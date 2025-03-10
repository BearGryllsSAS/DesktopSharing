// PHZ
// 2018-5-15

#ifndef XOP_SELECT_TASK_SCHEDULER_H
#define XOP_SELECT_TASK_SCHEDULER_H

/*
SelectTaskScheduler是XOP网络库中基于select的跨平台事件调度器，核心特点包括：

兼容性：支持Linux/Windows，适合低并发场景。
事件管理：通过备份fd_set优化性能，减少重复构建开销。
线程安全：使用互斥锁保护channels_，确保并发安全。
其设计权衡了性能与兼容性，是EpollTaskScheduler的补充方案。在实际应用中，需根据场景选择调度器：高并发用epoll，跨平台或低并发用select。
*/

#include "TaskScheduler.h"
#include "Socket.h"
#include <mutex>
#include <unordered_map>

#if defined(__linux) || defined(__linux__) 
#include <sys/select.h>
#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>
#endif

namespace xop
{	

/*
职责：基于select系统调用实现跨平台的事件驱动调度器，继承自TaskScheduler，管理多个Channel的事件监听与分发。
适用场景：低并发或需要跨平台（如Windows）的场景，相比EpollTaskScheduler，性能较低但兼容性更广。
*/
class SelectTaskScheduler : public TaskScheduler
{
public:
	SelectTaskScheduler(int id = 0);
	virtual ~SelectTaskScheduler();

	void UpdateChannel(ChannelPtr channel);
	void RemoveChannel(ChannelPtr& channel);
	bool HandleEvent(int timeout);
	
private:
	fd_set fd_read_backup_;		// 备份读事件的文件描述符集合，避免每次select调用前重新构建。
	fd_set fd_write_backup_;	// 备份写事件的文件描述符集合。
	fd_set fd_exp_backup_;		// 备份异常事件的文件描述符集合（如连接关闭）。
	SOCKET maxfd_ = 0;			// 当前监听的最大文件描述符值，用于select的第一个参数。

	// 标志位，表示是否需要重置对应的fd_set（读、写、异常）。
	bool is_fd_read_reset_ = false;
	bool is_fd_write_reset_ = false;
	bool is_fd_exp_reset_ = false;

	// 保护channels_的线程安全。
	std::mutex mutex_;
	// 存储Socket到Channel的映射，管理所有注册的Channel。
	std::unordered_map<SOCKET, ChannelPtr> channels_;
};

}

#endif
