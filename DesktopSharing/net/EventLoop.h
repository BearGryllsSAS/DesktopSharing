// PHZ
// 2018-5-15

#ifndef XOP_EVENT_LOOP_H
#define XOP_EVENT_LOOP_H

/*
实现了一个跨平台的事件循环（EventLoop），用于管理异步I/O事件、定时器和触发事件

核心机制：通过多线程任务调度器处理I/O、定时器和触发事件，支持跨平台。
使用场景：适用于需要高并发I/O处理的网络服务（如音视频流服务器）。
注意事项：需根据实际需求调整线程数和优先级，避免单调度器过载。
*/

#include <memory>
#include <atomic>
#include <unordered_map>
#include <functional>
#include <queue>
#include <thread>
#include <mutex>

#include "SelectTaskScheduler.h"
#include "EpollTaskScheduler.h"
#include "Pipe.h"
#include "Timer.h"
#include "RingBuffer.h"

#define TASK_SCHEDULER_PRIORITY_LOW       0
#define TASK_SCHEDULER_PRIORITY_NORMAL    1
#define TASK_SCHEDULER_PRIORITYO_HIGH     2 
#define TASK_SCHEDULER_PRIORITY_HIGHEST   3
#define TASK_SCHEDULER_PRIORITY_REALTIME  4

namespace xop
{

/*
多线程任务调度：支持多个任务调度器（TaskScheduler），每个调度器运行在独立线程中，通过轮询策略分配任务，提升并发性能。

跨平台支持：
Linux：使用EpollTaskScheduler（基于epoll的高效I/O多路复用）。
Windows：使用SelectTaskScheduler（基于select，兼容性较好但效率较低）。

优先级管理：允许设置线程优先级（Windows平台），支持实时性任务。
*/
class EventLoop 
{
public:
	EventLoop(const EventLoop&) = delete;
	EventLoop &operator = (const EventLoop&) = delete; 
	EventLoop(uint32_t num_threads =1);  //std::thread::hardware_concurrency()
	virtual ~EventLoop();

	std::shared_ptr<TaskScheduler> GetTaskScheduler();

	bool AddTriggerEvent(TriggerEvent callback);
	TimerId AddTimer(TimerEvent timerEvent, uint32_t msec);
	void RemoveTimer(TimerId timerId);	
	void UpdateChannel(ChannelPtr channel);
	void RemoveChannel(ChannelPtr& channel);
	
	void Loop();
	void Quit();

private:
	std::mutex mutex_;			// 互斥锁，保护对共享资源（如任务调度器列表、线程列表）的并发访问。
	uint32_t num_threads_ = 1;	// 事件循环的线程数，默认1，可通过构造函数指定。
	uint32_t index_ = 1;		// 轮询索引，用于在多线程场景下均衡分配任务调度器。
	std::vector<std::shared_ptr<TaskScheduler>> task_schedulers_;	// 存储所有任务调度器的向量，每个调度器管理一个事件循环线程。
	std::vector<std::shared_ptr<std::thread>> threads_;				// 存储所有线程对象的向量，每个线程运行一个任务调度器。
};

}

#endif