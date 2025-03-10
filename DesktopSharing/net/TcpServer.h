// PHZ
// 2018-11-10

#ifndef XOP_TCPSERVER_H
#define XOP_TCPSERVER_H

/*
TcpServer 类是一个 TCP 服务器，负责监听端口、接受客户端连接，并管理所有活跃的 TcpConnection 对象。其核心设计围绕 事件循环、连接管理 和 线程安全 展开。

1. 用户调用 Start(ip, port)
   └─→ Acceptor 开始监听
2. 新连接到达
   └─→ Acceptor 触发回调，创建 TcpConnection
   └─→ TcpConnection 注册到 EventLoop
   └─→ 连接加入 connections_ 映射
3. 数据到达/发送
   └─→ TcpConnection 的 HandleRead/HandleWrite 处理
4. 连接关闭
   └─→ 触发 DisconnectCallback，从 connections_ 移除
5. 用户调用 Stop()
   └─→ 关闭 Acceptor，断开所有连接，等待资源释放
*/

#include <memory>
#include <string>
#include <mutex>
#include <unordered_map>
#include "Socket.h"
#include "TcpConnection.h"

namespace xop
{

class Acceptor;
class EventLoop;

class TcpServer
{
public:	
	TcpServer(EventLoop* event_loop);
	virtual ~TcpServer();  

	// 启动服务器，开始监听指定 IP 和端口。
	virtual bool Start(std::string ip, uint16_t port);
	// 停止服务器，关闭所有连接并释放资源。
	virtual void Stop();

	std::string GetIPAddress() const
	{ return ip_; }

	uint16_t GetPort() const 
	{ return port_; }

protected:
	// 虚函数，创建 TcpConnection 对象（可被子类覆盖实现自定义连接逻辑）。
	virtual TcpConnection::Ptr OnConnect(SOCKET sockfd);
	// 将新连接添加到 connections_ 映射。
	virtual void AddConnection(SOCKET sockfd, TcpConnection::Ptr tcp_conn);
	// 从 connections_ 移除指定连接。
	virtual void RemoveConnection(SOCKET sockfd);

	EventLoop* event_loop_;					// 事件循环对象，驱动 Acceptor 和所有连接的 I/O 事件处理。
	uint16_t port_;
	std::string ip_;
	std::unique_ptr<Acceptor> acceptor_;	// 监听器，负责接受新连接。
	bool is_started_;
	std::mutex mutex_;						// 保护 connections_ 的线程安全访问。
	std::unordered_map<SOCKET, TcpConnection::Ptr> connections_;	// 存储所有活跃连接，键为套接字描述符。
};

}

#endif 
