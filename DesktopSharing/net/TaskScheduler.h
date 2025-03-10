// PHZ
// 2018-5-15

#ifndef XOP_TASK_SCHEDULER_H
#define XOP_TASK_SCHEDULER_H

/*
TaskScheduler是XOP网络库的任务调度核心，提供：

统一事件循环框架：整合I/O事件、定时任务和异步触发。
跨线程任务调度：通过管道和环形缓冲区实现线程间通信。
扩展性：子类可通过实现HandleEvent()支持不同I/O多路复用机制（如epoll、kqueue）。
其设计体现了高性能网络库的典型特征：事件驱动、异步回调、资源高效管理。实际使用中需注意资源释放和线程安全问题。
*/

#include "Channel.h"
#include "Pipe.h"
#include "Timer.h"
#include "RingBuffer.h"

namespace xop
{

typedef std::function<void(void)> TriggerEvent;

/*
职责：作为任务调度的基类，提供跨平台的事件循环框架，管理定时器、线程间唤醒和异步任务触发。
设计模式：结合Reactor模式（事件驱动）和Proactor模式（异步任务），支持：
I/O事件监听（由子类实现，如EpollTaskScheduler）。
定时任务调度。
跨线程任务触发（通过管道唤醒和环形缓冲区）。
*/
class TaskScheduler 
{
public:
	TaskScheduler(int id=1);
	virtual ~TaskScheduler();

	void Start();
	void Stop();
	TimerId AddTimer(TimerEvent timerEvent, uint32_t msec);
	void RemoveTimer(TimerId timerId);
	bool AddTriggerEvent(TriggerEvent callback);

	virtual void UpdateChannel(ChannelPtr channel) { };
	virtual void RemoveChannel(ChannelPtr& channel) { };
	virtual bool HandleEvent(int timeout) { return false; };

	int GetId() const 
	{ return id_; }

protected:
	void Wake();
	void HandleTriggerEvent();

	// 调度器唯一标识，用于多调度器场景（如多线程每个线程一个调度器）。
	int id_ = 0;
	// 原子标志位，控制事件循环的启停。
	std::atomic_bool is_shutdown_;
	// 管道（跨平台封装），用于线程间唤醒事件循环。
	std::unique_ptr<Pipe> wakeup_pipe_;
	// 关联wakeup_pipe_读端的Channel，监听唤醒事件。
	std::shared_ptr<Channel> wakeup_channel_;
	// 环形缓冲区，存储待触发的异步任务回调（线程安全）。
	std::unique_ptr<xop::RingBuffer<TriggerEvent>> trigger_events_;
	// 保护trigger_events_的线程安全。
	std::mutex mutex_;
	// 定时器队列，管理定时任务的添加、删除和执行。
	TimerQueue timer_queue_;

	static const char kTriggetEvent = 1;			// 唤醒事件的标识字符。
	static const char kTimerEvent = 2;				// 定时器事件的标识（未直接使用）。
	static const int  kMaxTriggetEvents = 50000;	// 环形缓冲区的最大容量，防止内存溢出。
};

}
#endif  

