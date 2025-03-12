// PHZ
// 2019-10-18

#include "EventLoop.h"

#if defined(WIN32) || defined(_WIN32) 
#include<windows.h>
#endif

#if defined(WIN32) || defined(_WIN32) 
#pragma comment(lib, "Ws2_32.lib")
#pragma comment(lib,"Iphlpapi.lib")
#endif 

using namespace xop;

/*
功能：初始化事件循环，指定线程数（默认1）。
关键操作：调用 Loop() 启动事件循环线程。
*/
EventLoop::EventLoop(uint32_t num_threads)
	: index_(1)
{
	num_threads_ = 1;
	if (num_threads > 0) {
		num_threads_ = num_threads;
	}

	this->Loop();
}

/*
功能：安全停止所有线程并清理资源。
关键操作：调用 Quit() 终止任务调度器并等待线程退出。
*/
EventLoop::~EventLoop()
{
	this->Quit();
}

/*
功能：获取一个任务调度器。
策略：
单线程：直接返回唯一的调度器。
多线程：轮询返回不同调度器，实现负载均衡。
用途：用户通过此接口将任务分配到不同线程。
*/
std::shared_ptr<TaskScheduler> EventLoop::GetTaskScheduler()
{
	std::lock_guard<std::mutex> locker(mutex_);
	if (task_schedulers_.size() == 1) {
		return task_schedulers_.at(0);
	}
	else {
		auto task_scheduler = task_schedulers_.at(index_);
		index_++;
		if (index_ >= task_schedulers_.size()) {
			index_ = 1;
		}		
		return task_scheduler;
	}

	return nullptr;
}

/*
核心功能：初始化并启动多线程事件循环，创建任务调度器（TaskScheduler），每个调度器运行在独立线程中，处理I/O事件、定时器和触发任务。
平台适配：根据操作系统选择高效的多路复用机制（Linux用epoll，Windows用select）。
线程优先级：设置线程优先级（仅Windows实现）。
*/
/*
EventLoop::Loop()
│
├── 加锁 (mutex_)
├── 检查是否已初始化 → 是则返回
├── 循环创建任务调度器（Epoll/Select）
│   ├── 创建 TaskScheduler 对象
│   ├── 启动线程执行 TaskScheduler::Start()
│   └── 存储调度器和线程
├── 设置线程优先级（仅Windows）
└── 解锁 (mutex_)
*/
void EventLoop::Loop()
{
	std::lock_guard<std::mutex> locker(mutex_);

	if (!task_schedulers_.empty()) {
		return ;
	}

	// 创建任务调度器与线程
	/*
	平台适配：
	Linux：使用 EpollTaskScheduler（基于 epoll 的高性能I/O多路复用）。
	Windows：使用 SelectTaskScheduler（基于 select，兼容但效率较低）。
	任务调度器存储：将调度器指针存入 task_schedulers_ 向量。
	线程启动：为每个调度器创建线程，执行 TaskScheduler::Start() 启动事件循环。
	冗余代码：thread->native_handle() 仅获取句柄但未使用，可删除。
	*/
	for (uint32_t n = 0; n < num_threads_; n++) 
	{
#if defined(__linux) || defined(__linux__) 
		std::shared_ptr<TaskScheduler> task_scheduler_ptr(new EpollTaskScheduler(n));
#elif defined(WIN32) || defined(_WIN32) 
		std::shared_ptr<TaskScheduler> task_scheduler_ptr(new SelectTaskScheduler(n));
#endif
		task_schedulers_.push_back(task_scheduler_ptr);
		std::shared_ptr<std::thread> thread(new std::thread(&TaskScheduler::Start, task_scheduler_ptr.get()));
		thread->native_handle();
		threads_.push_back(thread);

		// 在EventLoop里面，开一个线程去管理一个任务调度器
	}

	const int priority = TASK_SCHEDULER_PRIORITY_REALTIME;

	// 设置线程优先级（仅Windows）
	/*
	优先级常量：TASK_SCHEDULER_PRIORITY_REALTIME 对应最高实时优先级。
	Windows实现：通过 SetThreadPriority 设置线程优先级，提升关键任务响应速度。
	Linux缺失：未实现优先级设置，需补充 pthread 相关代码（如 pthread_setschedparam）。
	*/
	for (auto iter : threads_) 
	{
#if defined(__linux) || defined(__linux__) 

#elif defined(WIN32) || defined(_WIN32) 
		switch (priority) 
		{
		case TASK_SCHEDULER_PRIORITY_LOW:
			SetThreadPriority(iter->native_handle(), THREAD_PRIORITY_BELOW_NORMAL);
			break;
		case TASK_SCHEDULER_PRIORITY_NORMAL:
			SetThreadPriority(iter->native_handle(), THREAD_PRIORITY_NORMAL);
			break;
		case TASK_SCHEDULER_PRIORITYO_HIGH:
			SetThreadPriority(iter->native_handle(), THREAD_PRIORITY_ABOVE_NORMAL);
			break;
		case TASK_SCHEDULER_PRIORITY_HIGHEST:
			SetThreadPriority(iter->native_handle(), THREAD_PRIORITY_HIGHEST);
			break;
		case TASK_SCHEDULER_PRIORITY_REALTIME:
			SetThreadPriority(iter->native_handle(), THREAD_PRIORITY_TIME_CRITICAL);
			break;
		}
#endif
	}
}

/*
功能：停止事件循环。
流程：
调用所有任务调度器的 Stop() 终止事件循环。
等待所有线程结束（join()）并清空资源。
*/
void EventLoop::Quit()
{
	std::lock_guard<std::mutex> locker(mutex_);

	for (auto iter : task_schedulers_) {
		iter->Stop();
	}

	for (auto iter : threads_) {
		iter->join();
	}

	task_schedulers_.clear();
	threads_.clear();
}

/*
更新I/O通道（如Socket），仅由第一个调度器处理。
*/
void EventLoop::UpdateChannel(ChannelPtr channel)
{
	std::lock_guard<std::mutex> locker(mutex_);
	if (task_schedulers_.size() > 0) {
		task_schedulers_[0]->UpdateChannel(channel);
	}	
}

/*
移除I/O通道（如Socket），仅由第一个调度器处理。
*/
void EventLoop::RemoveChannel(ChannelPtr& channel)
{
	std::lock_guard<std::mutex> locker(mutex_);
	if (task_schedulers_.size() > 0) {
		task_schedulers_[0]->RemoveChannel(channel);
	}	
}

/*
添加定时器，由第一个调度器管理。
*/
TimerId EventLoop::AddTimer(TimerEvent timerEvent, uint32_t msec)
{
	std::lock_guard<std::mutex> locker(mutex_);
	if (task_schedulers_.size() > 0) {
		return task_schedulers_[0]->AddTimer(timerEvent, msec);
	}
	return 0;
}

/*
移除定时器，由第一个调度器管理。
*/
void EventLoop::RemoveTimer(TimerId timerId)
{
	std::lock_guard<std::mutex> locker(mutex_);
	if (task_schedulers_.size() > 0) {
		task_schedulers_[0]->RemoveTimer(timerId);
	}	
}

/*
添加立即触发的回调事件（如跨线程通知）。
*/
bool EventLoop::AddTriggerEvent(TriggerEvent callback)
{   
	std::lock_guard<std::mutex> locker(mutex_);
	if (task_schedulers_.size() > 0) {
		return task_schedulers_[0]->AddTriggerEvent(callback);
	}
	return false;
}
