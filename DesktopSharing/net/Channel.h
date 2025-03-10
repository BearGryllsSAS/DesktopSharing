// PHZ
// 2018-5-15
    
#ifndef XOP_CHANNEL_H
#define XOP_CHANNEL_H

/*
Channel类是XOP网络库的核心组件之一，负责将Socket的事件检测与处理逻辑解耦，通过回调机制提供灵活的事件驱动编程模型。
其设计简洁高效，适合构建高性能网络服务器。
*/

#include <functional>
#include <memory>
#include "Socket.h"

namespace xop
{

/*
作用：定义Socket可能触发的事件类型，使用位掩码组合（如EVENT_IN | EVENT_OUT）。
设计：值与Linux的epoll事件（如EPOLLIN、EPOLLOUT）对应，但做了跨平台抽象。
*/
enum EventType {
	EVENT_NONE = 0,     // 无事件
	EVENT_IN = 1,		// 可读事件（如数据到达）
	EVENT_PRI = 2,		// 高优先级数据（如带外数据）
	EVENT_OUT = 4,		// 可写事件
	EVENT_ERR = 8,		// 错误事件
	EVENT_HUP = 16,		// 连接挂起（如对端关闭）
	EVENT_RDHUP = 8192	// 对端关闭连接（EPOLLRDHUP的封装）
};

/*
职责：封装一个Socket文件描述符，管理其关注的事件（读、写、错误等），并通过回调函数处理事件。
设计模式：类似Reactor模式，将事件检测与处理逻辑解耦。
*/
class Channel 
{
public:
	typedef std::function<void()> EventCallback;
    
	// 强制通过Socket构造，确保每个Channel对象对应一个有效的Socket。
	Channel() = delete;

	Channel(SOCKET sockfd) 
		: sockfd_(sockfd)
	{

	}

	virtual ~Channel() {};
    
	// 允许用户为不同事件注册自定义处理逻辑。
	void SetReadCallback(const EventCallback& cb)
	{ read_callback_ = cb; }

	void SetWriteCallback(const EventCallback& cb)
	{ write_callback_ = cb; }

	void SetCloseCallback(const EventCallback& cb)
	{ close_callback_ = cb; }

	void SetErrorCallback(const EventCallback& cb)
	{ error_callback_ = cb; }

	SOCKET GetSocket() const { return sockfd_; }

	int  GetEvents() const { return events_; }
	void SetEvents(int events) { events_ = events; }
    
	// 通过位操作修改events_，控制事件监听状态。
	void EnableReading() 
	{ events_ |= EVENT_IN; }

	void EnableWriting() 
	{ events_ |= EVENT_OUT; }
    
	void DisableReading() 
	{ events_ &= ~EVENT_IN; }
    
	void DisableWriting() 
	{ events_ &= ~EVENT_OUT; }
       
	// 判断当前是否关注特定事件。
	bool IsNoneEvent() const { return events_ == EVENT_NONE; }
	bool IsWriting() const { return (events_ & EVENT_OUT) != 0; }
	bool IsReading() const { return (events_ & EVENT_IN) != 0; }
    
	// 根据传入的事件类型触发对应的回调。
	// 注意：EVENT_HUP处理后会直接返回，可能跳过后续事件（如同时发生的EVENT_ERR）。
	void HandleEvent(int events)
	{	
		if (events & (EVENT_PRI | EVENT_IN)) {
			read_callback_();
		}

		if (events & EVENT_OUT) {
			write_callback_();
		}
        
		if (events & EVENT_HUP) {
			close_callback_();
			return ;
		}

		if (events & (EVENT_ERR)) {
			error_callback_();
		}
	}

private:
	EventCallback read_callback_ = [] {};	// 读事件回调
	EventCallback write_callback_ = [] {};  // 写事件回调
	EventCallback close_callback_ = [] {};  // 关闭事件回调
	EventCallback error_callback_ = [] {};  // 错误事件回调
	
	SOCKET sockfd_ = 0;						// 关联的Socket句柄
	int events_ = 0;						// 当前关注的事件（位掩码）		如EVENT_IN | EVENT_OUT）
};

typedef std::shared_ptr<Channel> ChannelPtr;

}

#endif  

