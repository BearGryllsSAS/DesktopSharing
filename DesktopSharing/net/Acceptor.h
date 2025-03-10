#ifndef XOP_ACCEPTOR_H
#define XOP_ACCEPTOR_H

/*
Acceptor 类用于实现 TCP 服务器监听，管理监听套接字（SOCKET），通过事件循环（EventLoop）处理客户端连接请求，并通过回调机制将新连接传递给上层模块。

1. Listen() 初始化套接字并注册事件监听
   └─→ EventLoop 监控套接字可读事件
2. 新连接到达触发 OnAccept()
   └─→ 调用 accept() 获取新套接字
   └─→ 通过回调传递套接字给上层处理
3. Close() 注销事件监听并释放资源
*/

#include <functional>
#include <memory>
#include <mutex>
#include "Channel.h"
#include "TcpSocket.h"

namespace xop
{

typedef std::function<void(SOCKET)> NewConnectionCallback;

class EventLoop;

class Acceptor
{
public:	
	Acceptor(EventLoop* eventLoop);
	virtual ~Acceptor();

	// 设置新连接回调函数。
	void SetNewConnectionCallback(const NewConnectionCallback& cb)
	{ new_connection_callback_ = cb; }

	// 初始化监听套接字，绑定端口，注册事件到 EventLoop。
	int  Listen(std::string ip, uint16_t port);
	// 关闭监听套接字，从 EventLoop 注销事件监听。
	void Close();

private:
	// 处理新连接请求，触发回调函数。
	void OnAccept();

	EventLoop* event_loop_ = nullptr;				// 指向事件循环的指针，用于注册/注销监听事件。
	std::mutex mutex_;								// 互斥锁，保护 tcp_socket_ 和 channel_ptr_ 的线程安全访问。
	std::unique_ptr<TcpSocket> tcp_socket_;			// 封装监听套接字，管理其生命周期（自动释放资源）。
	ChannelPtr channel_ptr_;						// 封装套接字的事件监听（如可读事件），绑定回调函数 OnAccept()。
	NewConnectionCallback new_connection_callback_;	// 新连接到达时的回调函数，参数为新连接的套接字（SOCKET）。
};

}

#endif 
