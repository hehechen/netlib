#include "event_base.h"
#include "logging.h"
#include "sysutil.h"
#include <iostream>
#include <set>
#include <string.h>
#include <fcntl.h>
#include "poller.h"
#include "common.h"
namespace netlib {

PollerEpoll::PollerEpoll(){
    fd_ = epoll_create1(EPOLL_CLOEXEC); //当调用exec成功后，此fd自动关闭
    if(fd_<0)
        CHEN_LOG(ERROR,"create epoll error");
    CHEN_LOG(DEBUG,"poller epoll %d created", fd_);
}

PollerEpoll::~PollerEpoll() {
    CHEN_LOG(DEBUG,"destroying poller %d", fd_);
    while (liveChannels_.size()) {
        (*liveChannels_.begin())->close();
    }
    ::close(fd_);
   CHEN_LOG(DEBUG,"poller %d destroyed", fd_);
}

void PollerEpoll::addChannel(Channel* ch) {
    struct epoll_event ev;
    memset(&ev, 0, sizeof(ev));
    ev.events = ch->events();
    ev.data.ptr = ch;
    CHEN_LOG(DEBUG,"adding channel %lld fd %d events %d epoll %d", 
            (long long)ch->id(), ch->fd(), ev.events, fd_);
    int r = epoll_ctl(fd_, EPOLL_CTL_ADD, ch->fd(), &ev);
    if(r != 0)
        CHEN_LOG(ERROR, "epoll_ctl add failed ");
    liveChannels_.insert(ch);
}

void PollerEpoll::updateChannel(Channel* ch) {
    struct epoll_event ev;
    memset(&ev, 0, sizeof(ev));
    ev.events = ch->events();
    ev.data.ptr = ch;
    CHEN_LOG(DEBUG,"modifying channel %lld fd %d events read %d write %d epoll %d",
          (long long)ch->id(), ch->fd(), ev.events & POLLIN, ev.events & POLLOUT, fd_);
    int r = epoll_ctl(fd_, EPOLL_CTL_MOD, ch->fd(), &ev);
    if(r != 0)
        CHEN_LOG(ERROR,"epoll_ctl mod failed");
}

void PollerEpoll::removeChannel(Channel* ch) {
    CHEN_LOG(DEBUG,"deleting channel %lld fd %d epoll %d", 
                (long long)ch->id(), ch->fd(), fd_);
    liveChannels_.erase(ch);
    for (int i = lastActive_; i >= 0; i --) {
        if (ch == activeEvs_[i].data.ptr) {
            activeEvs_[i].data.ptr = NULL;
            break;
        }
    }
}

void PollerEpoll::loop_once(int waitMs) {
    int64_t ticks = sysutil::timeMilli();
    lastActive_ = epoll_wait(fd_, activeEvs_, kMaxEvents, waitMs);
    int64_t used = sysutil::timeMilli()-ticks;
    // CHEN_LOG(DEBUG,"epoll result:%d",lastActive_);
    // CHEN_LOG(DEBUG,"epoll wait %d return %d errno %d used %lld millsecond",
    //       waitMs, lastActive_, errno, (long long)used);
    if(lastActive_ == -1 && errno != EINTR)
       CHEN_LOG(ERROR, "epoll return error %d %s");
    while (--lastActive_ >= 0) {
        int i = lastActive_;
        Channel* ch = (Channel*)activeEvs_[i].data.ptr;
        int events = activeEvs_[i].events;
        if (ch) {
            if (events & (kReadEvent )) {
                CHEN_LOG(DEBUG,"channel %lld fd %d handle read", 
                        (long long)ch->id(), ch->fd());
                ch->handleRead();
            } else if (events & kWriteEvent) {
                CHEN_LOG(DEBUG,"channel %lld fd %d handle write",
                        (long long)ch->id(), ch->fd());
                ch->handleWrite();
            } else {
                CHEN_LOG(ERROR,"unexpected poller events");
            }
        }
    }
    lastActive_ = 0;
}
}