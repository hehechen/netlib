#pragma once
#include <poll.h>
#include <assert.h>
#include <set>
#include <atomic>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/epoll.h>
#include "common.h"
#include "event_base.h"
namespace netlib {

struct Channel;

const int kMaxEvents = 2000;

//PollerBase为基类，子类可以为epoll poll或其它操作系统的具体实现
class PollerBase: private noncopyable {
public:
    int lastActive_;
    PollerBase(): lastActive_(-1) { }
    virtual void addChannel(Channel* ch) = 0;
    virtual void removeChannel(Channel* ch) = 0;
    virtual void updateChannel(Channel* ch) = 0;
    virtual void loop_once(int waitMs) = 0;
    virtual ~PollerBase(){};
};

const int kReadEvent = EPOLLIN;
const int kWriteEvent = EPOLLOUT;

class PollerEpoll : public PollerBase{
public:
    int fd_;
    std::set<Channel*> liveChannels_;
    //for epoll selected active events
    struct epoll_event activeEvs_[kMaxEvents];
    PollerEpoll();
    ~PollerEpoll();
    void addChannel(Channel* ch) override;
    void removeChannel(Channel* ch) override;
    void updateChannel(Channel* ch) override;
    void loop_once(int waitMs) override;
};
#define PlatformPoller PollerEpoll
}