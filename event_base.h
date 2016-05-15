#pragma once
#include "slice.h"
#include "poller.h"
#include "common.h"
#include "sysutil.h"
namespace netlib {

class EventBases: private noncopyable {
public:
    virtual EventBase* allocBase() = 0;
};

//事件派发器，可管理定时器，连接，超时连接
class EventBase: public EventBases {    
public:
    //taskCapacity指定任务队列的大小，0无限制
    EventBase(int taskCapacity=0);
    ~EventBase();
    //处理已到期的事件,waitMs表示若无当前需要处理的任务，需要等待的时间
    void loop_once(int waitMs);
    //进入事件处理循环
    void loop() ;
    //取消定时任务，若timer已经过期，则忽略
    bool cancel(TimerId timerid);
    //添加定时任务，interval=0表示一次性任务，否则为重复任务，时间为毫秒，interval单位为秒
    TimerId runAt(int64_t microSecond, const Task& task, int64_t interval=0) { return runAt(microSecond, Task(task), interval); }
    TimerId runAt(int64_t microSecond, Task&& task, int64_t interval=0);
    TimerId runAfter(int64_t microSecond, const Task& task, int64_t interval=0) { return runAt(sysutil::timeMirco()+microSecond, Task(task), interval); }
    TimerId runAfter(int64_t microSecond, Task&& task, int64_t interval=0) { return runAt(sysutil::timeMirco()+microSecond, std::move(task), interval);}
    
    //下列函数为线程安全的

    //退出事件循环
    EventBase& exit();
    //是否已退出
    bool exited();
    //唤醒事件处理
    void wakeup();
    //添加任务
    void safeCall(Task&& task);
    void safeCall(const Task& task) { safeCall(Task(task)); }
    //分配一个事件派发器
    virtual EventBase* allocBase() { return this; }
    std::unique_ptr<EventsImp> imp_;
};

//多线程的事件派发器
class MultiBase: public EventBases{    
public:
    MultiBase(int sz): id_(0), bases_(sz) {}
    virtual EventBase* allocBase() { int c = id_++; return &bases_[c%bases_.size()]; }
    void loop();
    MultiBase& exit() { for (auto& b: bases_) { b.exit(); } return *this; }
private:
    std::atomic<int> id_;
    std::vector<EventBase> bases_;
};

//通道，封装了可以进行epoll的一个fd
class Channel: private noncopyable {    
public:
    //base为事件管理器，fd为通道内部的fd，events为通道关心的事件
    Channel(EventBase* base, int fd, int events);
    ~Channel();
    EventBase* getBase() { return base_; }
    int fd() { return fd_; }
    //通道id
    int64_t id() { return id_; }
    short events() { return events_; }
    //关闭通道
    void close();

    //挂接时间处理器
    void onRead(const Task& readcb) { readcb_ = readcb; }
    void onWrite(const Task& writecb) { writecb_ = writecb; }
    void onRead(Task&& readcb) { readcb_ = std::move(readcb); }
    void onWrite(Task&& writecb) { writecb_ = std::move(writecb); }

    //启用读写监听
    void enableRead(bool enable);
    void enableWrite(bool enable);
    void enableReadWrite(bool readable, bool writable);
    bool readEnabled();
    bool writeEnabled();

    //处理读写事件
    void handleRead() { readcb_(); }
    void handleWrite() { writecb_(); }
protected:
    EventBase* base_;
    PollerBase* poller_;
    int fd_;
    short events_;
    int64_t id_;
    std::function<void()> readcb_, writecb_, errorcb_;
};

}


#include "TimerHeap.h"
#include "conn.h"