#include "TcpServer.h"
#include "Acceptor.h"
#include "EventLoop.h"
#include "Logger.h"
#include <cstdio>  

using namespace xop;
using namespace std;

/*
设置 Acceptor 回调：当新连接到达时，调用 OnConnect() 创建 TcpConnection。
添加连接：将新连接加入 connections_ 映射。
设置断开回调：当连接关闭时，通过 AddTriggerEvent 或 AddTimer 异步移除连接，确保线程安全。
*/
TcpServer::TcpServer(EventLoop* event_loop)
	: event_loop_(event_loop)
	, port_(0)
	, acceptor_(new Acceptor(event_loop_))
	, is_started_(false)
{
	acceptor_->SetNewConnectionCallback([this](SOCKET sockfd) {
		// 传进来的 sockfd 已经 accept 过了，由 select 上的监听 socket 产生事件，然后 accpet 监听 socket 得到连接 sockfd
		// 用已经初始化好的连接 sokcet 去初始化 RtspConnection、TcpConnection 相关
		TcpConnection::Ptr conn = this->OnConnect(sockfd);
		if (conn) {
			this->AddConnection(sockfd, conn);
			conn->SetDisconnectCallback([this](TcpConnection::Ptr conn) {
				auto scheduler = conn->GetTaskScheduler();
				SOCKET sockfd = conn->GetSocket();
				// 尝试立即移除或延迟移除
				if (!scheduler->AddTriggerEvent([this, sockfd] {this->RemoveConnection(sockfd); })) {
					scheduler->AddTimer([this, sockfd]() {this->RemoveConnection(sockfd); return false; }, 100);	// 100ms 后重试
				}
			});
		}
	});
}

TcpServer::~TcpServer()
{
	Stop();
}

/*
调用 Acceptor::Listen() 启动监听。
更新服务器状态 (is_started_)。
*/
bool TcpServer::Start(std::string ip, uint16_t port)
{
	Stop();

	if (!is_started_) {
		if (acceptor_->Listen(ip, port) < 0) {
			return false;
		}

		port_ = port;
		ip_ = ip;
		is_started_ = true;
		return true;
	}

	return false;
}

void TcpServer::Stop()
{
	if (is_started_) {
		// 1. 断开所有连接
		mutex_.lock();
		for (auto iter : connections_) {
			iter.second->Disconnect(); // 异步关闭
		}
		mutex_.unlock();

		// 2. 关闭监听器
		acceptor_->Close();
		is_started_ = false;

		// 3. 等待所有连接资源释放
		while (1) {
			Timer::Sleep(10); // 休眠 10ms 避免忙等待
			if (connections_.empty()) {
				break;
			}
		}
	}	
}

TcpConnection::Ptr TcpServer::OnConnect(SOCKET sockfd)
{
	return std::make_shared<TcpConnection>(event_loop_->GetTaskScheduler().get(), sockfd);
}

void TcpServer::AddConnection(SOCKET sockfd, TcpConnection::Ptr tcpConn)
{
	std::lock_guard<std::mutex> locker(mutex_);
	connections_.emplace(sockfd, tcpConn);
}

void TcpServer::RemoveConnection(SOCKET sockfd)
{
	std::lock_guard<std::mutex> locker(mutex_);
	connections_.erase(sockfd);
}
