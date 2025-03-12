#include "Acceptor.h"
#include "EventLoop.h"
#include "SocketUtil.h"
#include "Logger.h"

using namespace xop;

Acceptor::Acceptor(EventLoop* eventLoop)
    : event_loop_(eventLoop)
    , tcp_socket_(new TcpSocket)
{	
	
}

Acceptor::~Acceptor()
{

}

/*
关闭旧套接字：确保每次调用 Listen() 时重新初始化。
设置套接字属性：
ReuseAddr/ReusePort：避免 TIME_WAIT 状态导致绑定失败。
NonBlock：非阻塞模式，防止 accept() 阻塞事件循环。
绑定与监听：调用 TcpSocket 的 Bind() 和 Listen()。
注册事件：
创建 Channel 对象，绑定读事件回调 OnAccept()。
启用读事件（EnableReading()），将 Channel 注册到 EventLoop。
*/
int Acceptor::Listen(std::string ip, uint16_t port) {
    std::lock_guard<std::mutex> locker(mutex_);

    // 1. 关闭旧套接字（如果存在）
    if (tcp_socket_->GetSocket() > 0) {
        tcp_socket_->Close();
    }

    // 2. 创建新套接字并设置属性
    SOCKET sockfd = tcp_socket_->Create();
    SocketUtil::SetReuseAddr(sockfd);  // 允许地址复用
    SocketUtil::SetReusePort(sockfd);  // 允许端口复用
    SocketUtil::SetNonBlock(sockfd);   // 非阻塞模式

    // 3. 绑定并监听端口
    if (!tcp_socket_->Bind(ip, port)) return -1;
    if (!tcp_socket_->Listen(1024)) return -1;

    // 4. 注册事件监听
    channel_ptr_.reset(new Channel(sockfd)); // 把监听 sockfd 做成 Channel 对象，赋值给 Acceptor 类中的 Channel 类型的智能指针成员变量
    channel_ptr_->SetReadCallback([this]() { this->OnAccept(); }); // 当监听 sockfd 有读事件（连接事件）的时候，调用 void Acceptor::OnAccept() 处理新的客户端连接
    channel_ptr_->EnableReading();
    // 加入select队列
    event_loop_->UpdateChannel(channel_ptr_); // event_loop_ 是 Acceptor 类中 EventLoop* 类型的成员变量，EventLoop 封装着线程容器、调度器容器（所有的任务都是在调度器中处理的）
   
    /*
    void EventLoop::UpdateChannel(ChannelPtr channel)
    {
	    std::lock_guard<std::mutex> locker(mutex_);
	    if (task_schedulers_.size() > 0) {
	    	task_schedulers_[0]->UpdateChannel(channel); // 会进入 void SelectTaskScheduler::UpdateChannel(ChannelPtr channel) 
                                                        // 最终执行 channels_.emplace(socket, channel)
                                                        // channels_ 类型为 std::unordered_map<SOCKET, ChannelPtr> channels_;
	    }	
    }
    */
    return 0;
}

/*
资源释放：从 EventLoop 移除 Channel，关闭套接字。
*/
void Acceptor::Close() {
    std::lock_guard<std::mutex> locker(mutex_);

    if (tcp_socket_->GetSocket() > 0) {
        event_loop_->RemoveChannel(channel_ptr_); // 注销事件监听
        tcp_socket_->Close();                     // 关闭套接字
    }
}

/*
功能：当监听套接字可读时（有新连接），调用 accept() 接受连接。
线程安全：通过 mutex_ 保护 tcp_socket_ 的 Accept() 操作。
回调处理：若设置了回调，将新套接字传递给上层；否则直接关闭。
*/
void Acceptor::OnAccept() {
    std::lock_guard<std::mutex> locker(mutex_);

    // tcp_socket_ 是 Acceptor 类中 TcpSocket 类型的成员变量，它的成员函数 sockfd_ 在 int Acceptor::Listen(std::string ip, uint16_t port) 中已经被设为监听套接字
    SOCKET socket = tcp_socket_->Accept();
    if (socket > 0) {
        if (new_connection_callback_) {
            new_connection_callback_(socket); // 传递新连接套接字
        }
        else {
            SocketUtil::Close(socket);        // 无回调则关闭连接
        }
    }
}

