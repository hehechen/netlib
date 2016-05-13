#include "TimerHeap.h"
#include "common.h"
#include "MutexLock.h"
#include "Condition.h"
#include "MutexLockGuard.h"

namespace netlib{

	//创建timerfd
	int createTimer()
	{
	  int timerfd = timerfd_create(CLOCK_MONOTONIC,	//系统重启到现在的时间
	                                 TFD_NONBLOCK | TFD_CLOEXEC);
	  if (timerfd < 0)
	  {
	    CHEN_LOG(ERROR,"create timerfd error");
	  }
	  return timerfd;
	}

	/*********堆的相关操作**************/
	typedef std::vector<Entry>::iterator Iterator;

	template<typename Comp>
	int push_heap(Iterator begin,Iterator end,Comp comp)
	{// 新加入结点  其父结点为(i - 1) / 2  
		int topIndex = 0;
		int len = end-begin;
		int holeIndex = len-1;
		int parent = (holeIndex - 1) / 2;	
		Entry value = *(end-1);
		while (holeIndex > topIndex && comp(*(begin + parent),*(begin + holeIndex)))
		{
			*(begin + holeIndex) = *(begin + parent);
			holeIndex = parent;
			parent = (holeIndex - 1) / 2;
		}
		*(begin + holeIndex) = value;
		return holeIndex;		
	}

	template<typename Comp>
	void adjust_heap(Iterator begin, Iterator end, int holeIndex, int len,Comp comp)
	{//从holeIndex开始调整
		int nextIndex = 2 * holeIndex + 1;
		while (nextIndex < len)
		{
			if (nextIndex < len - 1 && comp(*(begin + nextIndex), *(begin + (nextIndex + 1))))
				++nextIndex;
			if (comp(*(begin + nextIndex), *(begin + holeIndex)))
				break;
			std::swap(*(begin + nextIndex), *(begin + holeIndex));
			holeIndex = nextIndex;
			nextIndex = 2 * holeIndex + 1;
		}
	}
	template<typename Comp>
	void pop_heap(Iterator begin, Iterator end, Comp comp)
	{
		std::swap(*begin, *(end - 1));
		int len = end - begin - 1;
		adjust_heap(begin, end, 0, len,comp);
	}
	/*********堆的相关操作end**************/

	struct timespec howMuchTimeFromNow(TimeStamp when)
	{
	  int64_t microseconds = when.getMicrosecondsSinceEpoch() 
	  							- TimeStamp::now().getMicrosecondsSinceEpoch();
	  if (microseconds < 100)
	  {
	    microseconds = 100;
	  }
	  struct timespec ts;
	  ts.tv_sec = static_cast<time_t>(
	      microseconds / TimeStamp::kMicroSecondsPerSecond);
	  ts.tv_nsec = static_cast<long>(
	      (microseconds % TimeStamp::kMicroSecondsPerSecond) * 1000);
	  return ts;
	}

	//设置timerfd的超时时间
	void resetTimerfd(int timerfd, TimeStamp expiration)
	{
	  struct itimerspec newValue;
	  struct itimerspec oldValue;
	  bzero(&newValue, sizeof newValue);
	  bzero(&oldValue, sizeof oldValue);
	  newValue.it_value = howMuchTimeFromNow(expiration);
	  int ret = ::timerfd_settime(timerfd, 0, &newValue, &oldValue);
	  if (ret)
	  {
	    CHEN_LOG(ERROR,"set timerfd error");
	  }
	}


	TimerHeap::TimerHeap(EventBase* base):cond(mutex),
							timerFD(createTimer()),timerChannel(base,timerFD,kReadEvent)
	{
	}

	TimerHeap::~TimerHeap()
	{
		close(timerFD);
	}

	TimerId TimerHeap::addTimer(TimeStamp when,TimerCallback cb)
	{
		MutexLockGuard mutexLock(mutex);		
		timers.push_back(Entry(when,cb));
		int index = push_heap(timers.begin(),timers.end(),EntryComp());
		resetTimerfd(timerFD,timers.begin()->first);
		//保存此定时器在timers中的位置并设置timerID
		TimerId ids_size = timerIDs.size();
		for(TimerId i=0;i<ids_size;i++)
			if(timerIDs[i] == -1)
			{
				timerIDs[i] = index;
				CHEN_LOG(DEBUG,"id:%d",i);
				return i;
			}
		timerIDs.push_back(index);

		//注册定时事件
		timerChannel.onRead(std::bind(&TimerHeap::handle_read,this));
		timerChannel.enableRead(true);
		CHEN_LOG(DEBUG,"id:%d",ids_size);
		return ids_size;	
	}

	bool TimerHeap::cancel(TimerId timerId)
	{
		MutexLockGuard mutexLock(mutex);
		int index = timerIDs[timerId];
		if(-1 == index)
			return false;
		std::swap(timers[index],timers[timers.size()-1]);
		timers.pop_back();
		adjust_heap(timers.begin(),timers.end(),index,timers.size(),EntryComp());
		timerIDs[timerId] = -1;
		return true;
	}

	void TimerHeap::handle_read()
	{
		//把超时状态读取掉
		uint64_t value;
		read(timerFD, &value, 8);
		CHEN_LOG(DEBUG,"read results:%lld",value);
		reset();	
	}

	//执行定时器的任务并将已到时间的定时器清除
	void TimerHeap::reset()
	{
		TimeStamp now = timers.begin()->first;
		while (!timers.empty() && timers.begin()->first == now)
		{//处理同时任务
			timers.begin()->second();
			pop_heap(timers.begin(), timers.end(), EntryComp());
			timers.pop_back();
		}
		if(!timers.empty())
			resetTimerfd(timerFD,timers.begin()->first);
	}
}
