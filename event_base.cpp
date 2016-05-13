#include <map>
#include <thread>
#include <string.h>
#include <fcntl.h>
#include "event_base.h"
#include "logging.h"
#include "common.h"
#include "sysutil.h"
#include "poller.h"
#include "MutexLockGuard.h"
using namespace std;

namespace netlib {

class EventsImp {    
public:
    EventBase* base_;
    PollerBase* poller_;
    std::atomic<bool> exit_;
    int wakeupFds_[2];
    int nextTimeout_;
    std::vector<Task> tasks_;    
    std::unique_ptr<TimerHeap> timerHeap;
    bool idleEnabled;

    EventsImp(EventBase* base, int taskCap);
    ~EventsImp();
    void init();
    void handleTimeouts();

    //eventbase functions
    EventBase& exit() {exit_ = true; wakeup(); return *base_;}
    bool exited() { return exit_; }
    void safeCall(Task&& task) 
    { 
        {
            MutexLockGuard lock(mutex);
            tasks_.push_back(move(task));    
        }        
        wakeup(); 
    }
    void loop();
    void loop_once(int waitMs) { poller_->loop_once(std::min(waitMs, nextTimeout_)); }
    void wakeup() {
        int r = write(wakeupFds_[1], "", 1);
        if(r<=0)
            CHEN_LOG(ERROR,"write error wd %d", r);
    }

    bool cancel(TimerId timerid);
    TimerId runAt(int64_t microSecond, Task&& task, int64_t interval);
private:
    MutexLock mutex;    //互斥锁
};

EventBase::EventBase(int taskCapacity) {
    imp_.reset(new EventsImp(this, taskCapacity));
    imp_->init();    
    imp_->timerHeap.reset(new TimerHeap(this));
}

EventBase::~EventBase() {}

EventBase& EventBase::exit() { return imp_->exit(); }

bool EventBase::exited() { return imp_->exited(); }

void EventBase::safeCall(Task&& task) { imp_->safeCall(move(task)); }

void EventBase::wakeup(){ imp_->wakeup(); }

void EventBase::loop() { imp_->loop(); }

void EventBase::loop_once(int waitMs) { imp_->loop_once(waitMs); }

bool EventBase::cancel(TimerId timerid) { return imp_ && imp_->cancel(timerid); }

TimerId EventBase::runAt(int64_t microSecond, Task&& task, int64_t interval) {
    return imp_->runAt(microSecond, std::move(task), interval); 
}

EventsImp::EventsImp(EventBase* base, int taskCap):
    base_(base), poller_(new PlatformPoller()), exit_(false), nextTimeout_(1<<30),
            idleEnabled(false)
{
}

void EventsImp::loop() {
    while (!exit_)
        loop_once(10000);

    loop_once(0);
}

void EventsImp::init() {
    int r = pipe(wakeupFds_);
    if(r != 0)
        CHEN_LOG(ERROR,"pipe failed");
    r = sysutil::addFdFlag(wakeupFds_[0], FD_CLOEXEC);
    r = sysutil::addFdFlag(wakeupFds_[1], FD_CLOEXEC);
    CHEN_LOG(DEBUG,"wakeup pipe created %d %d", wakeupFds_[0], wakeupFds_[1]);
    Channel* ch = new Channel(base_, wakeupFds_[0], kReadEvent);
    ch->onRead([=] {
        char buf[1024];
        int r = ch->fd() >= 0 ? ::read(ch->fd(), buf, sizeof buf) : 0;
        if (r > 0) {
            std::vector<Task> functors;
            {
                MutexLockGuard lock(mutex);
                functors.swap(tasks_);
            }
            for(int i=0;i<functors.size();i++)
                functors[i]();
        } else if (r == 0) {
            delete ch;
        } else if (errno == EINTR) {
        } else {
            CHEN_LOG(ERROR,"wakeup channel read error %d", r);
        }
    });
}

EventsImp::~EventsImp() {
    delete poller_;
    ::close(wakeupFds_[1]);
}

TimerId EventsImp::runAt(int64_t microSecond, Task&& task, int64_t interval) {
    return timerHeap->addTimer(TimeStamp(microSecond),task);
}

bool EventsImp::cancel(TimerId timerid) {
    return timerHeap->cancel(timerid);
}

void MultiBase::loop() {
    int sz = bases_.size();
    vector<thread> ths(sz -1);
    for(int i = 0; i < sz -1; i++) {
        thread t([this, i]{ bases_[i].loop();});
        ths[i].swap(t);
    }
    bases_.back().loop();
    for (int i = 0; i < sz -1; i++) {
        ths[i].join();
    }
}

Channel::Channel(EventBase* base, int fd, int events): base_(base), fd_(fd), events_(events) {
    if(sysutil::setNonBlock(fd_) < 0)
        CHEN_LOG(ERROR, "channel set non block failed");
    static atomic<int64_t> id(0);
    id_ = ++id;
    poller_ = base_->imp_->poller_;
    poller_->addChannel(this);
}

Channel::~Channel() {
    close();
}

void Channel::enableRead(bool enable) {
    if (enable) {
        events_ |= kReadEvent;
    } else {
        events_ &= ~kReadEvent;
    }
    poller_->updateChannel(this);
}

void Channel::enableWrite(bool enable) {
    if (enable) {
        events_ |= kWriteEvent;
    } else {
        events_ &= ~kWriteEvent;
    }
    poller_->updateChannel(this);
}

void Channel::enableReadWrite(bool readable, bool writable) {
    if (readable) {
        events_ |= kReadEvent;
    } else {
        events_ &= ~kReadEvent;
    }
    if (writable) {
        events_ |= kWriteEvent;
    } else {
        events_ &= ~kWriteEvent;
    }
    poller_->updateChannel(this);
}

void Channel::close() {
    if (fd_>=0) {
        CHEN_LOG(DEBUG,"close channel %ld fd %d", (long)id_, fd_);
        poller_->removeChannel(this);
        ::close(fd_);
        fd_ = -1;
        handleRead();
    }
}

bool Channel::readEnabled() { return events_ & kReadEvent; }
bool Channel::writeEnabled() { return events_ & kWriteEvent; }


TcpConn::TcpConn()
:base_(NULL), channel_(NULL), state_(State::Invalid), destPort_(-1),
 connectTimeout_(0), reconnectInterval_(-1),connectedTime_(sysutil::timeMilli())
{
}

TcpConn::~TcpConn() {
    CHEN_LOG(DEBUG,"tcp destroyed %s - %s", local_.toString().c_str(), peer_.toString().c_str());
    delete channel_;
}

// void TcpConn::addIdleCB(int idle, const TcpCallBack& cb) {
//     if (channel_) {
//         idleIds_.push_back(getBase()->imp_->registerIdle(idle, shared_from_this(), cb));
//     }
// }

// void TcpConn::reconnect() {
//     auto con = shared_from_this();
//     getBase()->imp_->reconnectConns_.insert(con);
//     int64_t interval = reconnectInterval_-(sysutil::timeMilli()-connectedTime_);
//     interval = interval>0?interval:0;
//     CHEN_LOG(DEBUG,"reconnect interval: %d will reconnect after %lld ms", reconnectInterval_, interval);
//     getBase()->runAfter(interval, [this, con]() {
//         getBase()->imp_->reconnectConns_.erase(con);
//         connect(getBase(), destHost_, (short)destPort_, connectTimeout_, localIp_);
//     });
//     delete channel_;
//     channel_ = NULL;
// }

}