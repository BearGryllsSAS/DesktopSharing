// PHZ
// 2018-5-15

#ifndef XOP_EPOLL_TASK_SCHEDULER_H
#define XOP_EPOLL_TASK_SCHEDULER_H

/*
EpollTaskScheduler是XOP网络库中基于Linux epoll实现的事件调度器，核心功能包括：

管理多个Channel的事件注册与删除。
通过epoll_wait监听事件，高效分发到对应的Channel处理。
提供线程安全的Channel更新接口。
其设计简洁高效，但需注意资源管理和错误处理的完善。在实际使用中，通常作为网络服务器的事件循环核心组件，配合Channel实现高性能异步I/O。
*/

#include "TaskScheduler.h"
#include <mutex>
#include <unordered_map>

namespace xop
{
/*
职责：基于Linux的epoll实现的高效I/O事件驱动调度器，继承自TaskScheduler，负责管理多个Channel的事件监听与分发。
设计模式：Reactor模式，通过epoll实现多路复用，监听Socket事件并触发对应的Channel回调。
*/
class EpollTaskScheduler : public TaskScheduler
{
public:
	EpollTaskScheduler(int id = 0);
	virtual ~EpollTaskScheduler();

	void UpdateChannel(ChannelPtr channel);
	void RemoveChannel(ChannelPtr& channel);

	// timeout: ms
	bool HandleEvent(int timeout);

private:
	void Update(int operation, ChannelPtr& channel);

	int epollfd_ = -1;
	std::mutex mutex_;
	std::unordered_map<int, ChannelPtr> channels_;	// 维护Socket文件描述符（int）到Channel对象的映射，用于快速查找和管理所有注册的Channel。
};

}

#endif
