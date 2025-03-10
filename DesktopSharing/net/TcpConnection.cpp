#include "TcpConnection.h"
#include "SocketUtil.h"

using namespace xop;

/*
设置非阻塞模式，避免 I/O 操作阻塞事件循环。
调整发送缓冲区大小，提升吞吐量。
启用 TCP Keep-Alive，检测死连接。
通过 Lambda 绑定事件处理函数，注册到 TaskScheduler。
*/
// 对连接 socket 进行一系列操作
TcpConnection::TcpConnection(TaskScheduler* task_scheduler, SOCKET sockfd)
	: task_scheduler_(task_scheduler), channel_(new Channel(sockfd))
{
	// 初始化缓冲区
	read_buffer_.reset(new BufferReader);
	write_buffer_.reset(new BufferWriter(500)); // 初始容量 500 字节

	// 设置非阻塞和TCP参数
	SocketUtil::SetNonBlock(sockfd);
	SocketUtil::SetSendBufSize(sockfd, 100 * 1024); // 发送缓冲区 100KB
	SocketUtil::SetKeepAlive(sockfd);               // 开启 Keep-Alive

	// 绑定事件回调
	channel_->SetReadCallback([this]() { HandleRead(); });
	channel_->SetWriteCallback([this]() { HandleWrite(); });
	channel_->SetCloseCallback([this]() { HandleClose(); });
	channel_->SetErrorCallback([this]() { HandleError(); });

	// 注册读事件到 EventLoop
	channel_->EnableReading();
	task_scheduler_->UpdateChannel(channel_);
}

TcpConnection::~TcpConnection()
{
	SOCKET fd = channel_->GetSocket();
	if (fd > 0) {
		SocketUtil::Close(fd);
	}
}

void TcpConnection::Send(std::shared_ptr<char> data, uint32_t size)
{
	if (!is_closed_) {
		mutex_.lock();
		write_buffer_->Append(data, size);
		mutex_.unlock();

		this->HandleWrite();
	}
}

/*
线程安全：mutex_ 保护 write_buffer_，避免多线程竞争。
立即发送优化：调用 HandleWrite() 尝试直接发送数据，减少延迟。
*/
void TcpConnection::Send(const char *data, uint32_t size)
{
	if (!is_closed_) {
		mutex_.lock();
		write_buffer_->Append(data, size); // 数据追加到写缓冲区
		mutex_.unlock();

		this->HandleWrite(); // 立即尝试发送
	}
}

void TcpConnection::Disconnect()
{
	std::lock_guard<std::mutex> lock(mutex_);
	auto conn = shared_from_this();
	task_scheduler_->AddTriggerEvent([conn]() {
		conn->Close();
	});
}

void TcpConnection::HandleRead()
{
	{
		std::lock_guard<std::mutex> lock(mutex_);

		if (is_closed_) {
			return;
		}
		
		int ret = read_buffer_->Read(channel_->GetSocket());
		if (ret <= 0) {
			this->Close();
			return;
		}
	}

	if (read_cb_) {
		bool ret = read_cb_(shared_from_this(), *read_buffer_);
		if (false == ret) {
			std::lock_guard<std::mutex> lock(mutex_);
			this->Close();
		}
	}
}

/*
写事件处理：HandleWrite()

性能优化：
非阻塞加锁：try_lock 避免阻塞事件循环。
按需注册写事件：仅在缓冲区非空时监听 EPOLLOUT，减少无效事件触发。
错误处理：发送失败（如 errno == ECONNRESET）立即关闭连接。
*/
void TcpConnection::HandleWrite() {
	if (is_closed_) return;

	if (!mutex_.try_lock()) return; // 非阻塞尝试加锁

	int ret = 0;
	bool empty = false;
	do {
		ret = write_buffer_->Send(channel_->GetSocket()); // 发送数据
		if (ret < 0) { // 发送失败（如连接断开）
			Close();
			mutex_.unlock();
			return;
		}
		empty = write_buffer_->IsEmpty();
	} while (0); // 可扩展为循环发送（如非阻塞模式下多次发送）

	// 动态注册/注销写事件
	if (empty) {
		if (channel_->IsWriting()) {
			channel_->DisableWriting(); // 无数据可写，禁用写事件
			task_scheduler_->UpdateChannel(channel_);
		}
	}
	else if (!channel_->IsWriting()) {
		channel_->EnableWriting(); // 有数据待写，注册写事件
		task_scheduler_->UpdateChannel(channel_);
	}

	mutex_.unlock();
}

/*
从 TaskScheduler 移除 channel_，停止事件监听。
通过 shared_from_this() 传递自身指针，确保回调执行期间对象存活。
*/
void TcpConnection::Close() 
{
	if (!is_closed_) {
		is_closed_ = true;
		task_scheduler_->RemoveChannel(channel_); // 注销事件监听

		// 触发用户回调和上层清理
		if (close_cb_) close_cb_(shared_from_this());
		if (disconnect_cb_) disconnect_cb_(shared_from_this());
	}
}


void TcpConnection::HandleClose()
{
	std::lock_guard<std::mutex> lock(mutex_);
	this->Close();
}

void TcpConnection::HandleError()
{
	std::lock_guard<std::mutex> lock(mutex_);
	this->Close();
}
