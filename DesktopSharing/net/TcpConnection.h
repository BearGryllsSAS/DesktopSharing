#ifndef XOP_TCP_CONNECTION_H
#define XOP_TCP_CONNECTION_H

/*
TcpConnection 类管理单个 TCP 连接的生命周期，处理数据读写、连接关闭和错误事件。通过事件驱动模型与 TaskScheduler 协同工作，实现非阻塞 I/O 操作。

1. Acceptor 接收到新连接（OnAccept()）
   └─→ 创建 TcpConnection 对象，socket 传递给构造函数
2. TcpConnection 初始化
   └─→ 注册 socket 读事件到 TaskScheduler
3. 数据到达（HandleRead()）
   └─→ 读取到 read_buffer_
   └─→ 触发 read_cb_，用户处理数据
4. 用户调用 Send() 发送数据
   └─→ 写入 write_buffer_，触发 HandleWrite()
5. 连接关闭（用户主动或异常）
   └─→ 触发 Close()，清理资源并回调通知上层
*/

#include <atomic>
#include <mutex>
#include "TaskScheduler.h"
#include "BufferReader.h"
#include "BufferWriter.h"
#include "Channel.h"
#include "SocketUtil.h"

namespace xop
{

class TcpConnection : public std::enable_shared_from_this<TcpConnection>
{
public:
	using Ptr = std::shared_ptr<TcpConnection>;
	using DisconnectCallback = std::function<void(std::shared_ptr<TcpConnection> conn)> ;
	using CloseCallback = std::function<void(std::shared_ptr<TcpConnection> conn)>;
	using ReadCallback = std::function<bool(std::shared_ptr<TcpConnection> conn, xop::BufferReader& buffer)>;

	TcpConnection(TaskScheduler *task_scheduler, SOCKET sockfd);
	virtual ~TcpConnection();

	TaskScheduler* GetTaskScheduler() const 
	{ return task_scheduler_; }

	void SetReadCallback(const ReadCallback& cb)
	{ read_cb_ = cb; }

	void SetCloseCallback(const CloseCallback& cb)
	{ close_cb_ = cb; }

	void Send(std::shared_ptr<char> data, uint32_t size);
	void Send(const char *data, uint32_t size);
    
	void Disconnect();

	bool IsClosed() const 
	{ return is_closed_; }

	SOCKET GetSocket() const
	{ return channel_->GetSocket(); }

	uint16_t GetPort() const
	{ return SocketUtil::GetPeerPort(channel_->GetSocket()); }
    
	std::string GetIp() const
	{ return SocketUtil::GetPeerIp(channel_->GetSocket()); }

protected:
	friend class TcpServer;

	virtual void HandleRead();		// 处理读事件：接收数据并触发 read_cb_。
	virtual void HandleWrite();		// 处理写事件：发送 write_buffer_ 中的数据，优化 EPOLLOUT 事件注册。
	virtual void HandleClose();		// 
	virtual void HandleError();		// 

	void SetDisconnectCallback(const DisconnectCallback& cb)
	{ disconnect_cb_ = cb; }

	TaskScheduler* task_scheduler_;						// 所属任务调度器，管理事件监听和线程执行。
	std::unique_ptr<xop::BufferReader> read_buffer_;	// 读缓冲区，用于接收数据。
	std::unique_ptr<xop::BufferWriter> write_buffer_;	// 写缓冲区，用于暂存待发送数据。
	std::atomic_bool is_closed_;						// 原子标记连接是否已关闭，保证线程安全。

private:
	void Close();								// 关闭连接，清理资源并触发回调。

	std::shared_ptr<xop::Channel> channel_;		// 封装 socket 的事件监听（读、写、关闭、错误）。
	std::mutex mutex_;							// 保护 write_buffer_ 和状态变更的线程安全。
	DisconnectCallback disconnect_cb_;			// 连接断开时的回调（通常由 TcpServer 设置）。
	CloseCallback close_cb_;					// 连接关闭时的回调（用户自定义）。
	ReadCallback read_cb_;						// 数据到达时的回调（用户自定义，返回 false 则关闭连接）。
};

}

#endif 
