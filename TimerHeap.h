#ifndef TIMER_HEAP_H
#define TIMER_HEAP_H
#include <functional>
#include <vector>
#include "event_base.h"
#include "TimeStamp.h"
#include "MutexLockGuard.h"
#include "Condition.h"
#include "common.h"
//用最小堆和timerfd实现定时器
//每个进程一个定时器，可以设为静态

namespace netlib{


    typedef std::function<void()> TimerCallback;
    typedef std::pair<TimeStamp,TimerCallback> Entry;

    struct EntryComp{
        bool operator()(const Entry &a,const Entry &b)
        {
            return a.first > b.first;
        }
    };
    class TimerHeap
    {
    public:
        TimerHeap(EventBase* base);    //开启监听线程
        ~TimerHeap();
        int getTimerFd()    { return timerFD; }
        TimerId addTimer(TimeStamp when,TimerCallback cb);
        bool cancel(TimerId id);

    private:
        MutexLock mutex;    //互斥锁
        Condition cond;  //条件变量
        int timerFD;       //堆顶的Timerfd
        Channel timerChannel;
        //堆的底层结构,根据超时时间排序,因此只需监听第一个是否超时即可
        std::vector<Entry> timers;   
        //  addTimer后返回TimerId，用来区分定时任务，方便cancle 
        //timerIDs的pos为TimerId，内容为timers的pos，如果被cancle则置为-1
        std::vector<int> timerIDs;        
        void *loop_timer(void*);
        //当时间到了，timerfd变为可读时执行此函数，在这个函数里执行用户注册的回调函数
        //并修改set的内容
        void handle_read(); 
        void reset();       //更新定时器set
    };


}
#endif // TIMER_HEAP_H
