// PHZ
// 2018-5-15

#include "EpollTaskScheduler.h"

#if defined(__linux) || defined(__linux__) 
#include <sys/epoll.h>
#include <errno.h>
#endif

using namespace xop;

/*
创建epoll实例，初始大小1024。
注册wakeup_channel_（基类成员，可能用于跨线程唤醒事件循环）。
*/
EpollTaskScheduler::EpollTaskScheduler(int id)
	: TaskScheduler(id)
{
#if defined(__linux) || defined(__linux__) 
    epollfd_ = epoll_create(1024);
 #endif
    this->UpdateChannel(wakeup_channel_);
}

EpollTaskScheduler::~EpollTaskScheduler()
{
	
}

/*
功能：添加或更新Channel的事件监听。
逻辑：
若Channel已存在：
若事件为EVENT_NONE，从epoll和channels_中删除。
否则，更新epoll监听事件。
若Channel不存在且有关注的事件，添加到epoll和channels_。
*/
void EpollTaskScheduler::UpdateChannel(ChannelPtr channel)
{
	std::lock_guard<std::mutex> lock(mutex_);
#if defined(__linux) || defined(__linux__) 
	int fd = channel->GetSocket();
	if(channels_.find(fd) != channels_.end()) {
		if(channel->IsNoneEvent()) {
			Update(EPOLL_CTL_DEL, channel);
			channels_.erase(fd);
		}
		else {
			Update(EPOLL_CTL_MOD, channel);
		}
	}
	else {
		if(!channel->IsNoneEvent()) {
			channels_.emplace(fd, channel);
			Update(EPOLL_CTL_ADD, channel);
		}	
	}	
#endif
}

/*
功能：封装epoll_ctl系统调用，向epoll实例注册/修改/删除事件。
关键点：
event.data.ptr存储Channel对象的原始指针，用于事件触发时快速定位处理对象。
未处理epoll_ctl的错误（如日志记录或异常抛出）。
*/
void EpollTaskScheduler::Update(int operation, ChannelPtr& channel)
{
#if defined(__linux) || defined(__linux__) 
	struct epoll_event event = {0};

	if(operation != EPOLL_CTL_DEL) {
		event.data.ptr = channel.get();
		event.events = channel->GetEvents();
	}

	if(::epoll_ctl(epollfd_, operation, channel->GetSocket(), &event) < 0) {

	}
#endif
}

/*
功能：从epoll和channels_中移除指定Channel。
用途：当连接关闭或不再需要监听事件时调用。
*/
void EpollTaskScheduler::RemoveChannel(ChannelPtr& channel)
{
    std::lock_guard<std::mutex> lock(mutex_);
#if defined(__linux) || defined(__linux__) 
	int fd = channel->GetSocket();

	if(channels_.find(fd) != channels_.end()) {
		Update(EPOLL_CTL_DEL, channel);
		channels_.erase(fd);
	}
#endif
}

/*
功能：核心事件循环，阻塞等待事件并处理。
流程：
调用epoll_wait等待事件，超时时间由参数指定（毫秒）。
遍历就绪事件，通过event.data.ptr获取关联的Channel对象。
调用Channel::HandleEvent()处理具体事件（如读、写、错误）。
关键点：
每次最多处理512个事件（events数组大小）。
处理信号中断（EINTR）时正常返回。
*/
bool EpollTaskScheduler::HandleEvent(int timeout)
{
#if defined(__linux) || defined(__linux__) 
	struct epoll_event events[512] = {0};
	int num_events = -1;

	num_events = epoll_wait(epollfd_, events, 512, timeout);
	if(num_events < 0)  {
		if(errno != EINTR) {	
			return false;	// 忽略信号中断
		}								
	}

	for(int n=0; n<num_events; n++) {
		if(events[n].data.ptr) {        
			((Channel *)events[n].data.ptr)->HandleEvent(events[n].events);
		}
	}		
	return true;
#else
    return false;
#endif
}


