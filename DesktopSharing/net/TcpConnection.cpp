#include "TcpConnection.h"
#include "SocketUtil.h"

using namespace xop;

/*
���÷�����ģʽ������ I/O ���������¼�ѭ����
�������ͻ�������С��������������
���� TCP Keep-Alive����������ӡ�
ͨ�� Lambda ���¼���������ע�ᵽ TaskScheduler��
*/
// ������ socket ����һϵ�в���
TcpConnection::TcpConnection(TaskScheduler* task_scheduler, SOCKET sockfd)
	: task_scheduler_(task_scheduler), channel_(new Channel(sockfd))
{
	// ��ʼ��������
	read_buffer_.reset(new BufferReader);
	write_buffer_.reset(new BufferWriter(500)); // ��ʼ���� 500 �ֽ�

	// ���÷�������TCP����
	SocketUtil::SetNonBlock(sockfd);
	SocketUtil::SetSendBufSize(sockfd, 100 * 1024); // ���ͻ����� 100KB
	SocketUtil::SetKeepAlive(sockfd);               // ���� Keep-Alive

	// ���¼��ص�
	channel_->SetReadCallback([this]() { HandleRead(); });
	channel_->SetWriteCallback([this]() { HandleWrite(); });
	channel_->SetCloseCallback([this]() { HandleClose(); });
	channel_->SetErrorCallback([this]() { HandleError(); });

	// ע����¼��� EventLoop
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
�̰߳�ȫ��mutex_ ���� write_buffer_��������߳̾�����
���������Ż������� HandleWrite() ����ֱ�ӷ������ݣ������ӳ١�
*/
void TcpConnection::Send(const char *data, uint32_t size)
{
	if (!is_closed_) {
		mutex_.lock();
		write_buffer_->Append(data, size); // ����׷�ӵ�д������
		mutex_.unlock();

		this->HandleWrite(); // �������Է���
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
д�¼�����HandleWrite()

�����Ż���
������������try_lock ���������¼�ѭ����
����ע��д�¼������ڻ������ǿ�ʱ���� EPOLLOUT��������Ч�¼�������
����������ʧ�ܣ��� errno == ECONNRESET�������ر����ӡ�
*/
void TcpConnection::HandleWrite() {
	if (is_closed_) return;

	if (!mutex_.try_lock()) return; // ���������Լ���

	int ret = 0;
	bool empty = false;
	do {
		ret = write_buffer_->Send(channel_->GetSocket()); // ��������
		if (ret < 0) { // ����ʧ�ܣ������ӶϿ���
			Close();
			mutex_.unlock();
			return;
		}
		empty = write_buffer_->IsEmpty();
	} while (0); // ����չΪѭ�����ͣ��������ģʽ�¶�η��ͣ�

	// ��̬ע��/ע��д�¼�
	if (empty) {
		if (channel_->IsWriting()) {
			channel_->DisableWriting(); // �����ݿ�д������д�¼�
			task_scheduler_->UpdateChannel(channel_);
		}
	}
	else if (!channel_->IsWriting()) {
		channel_->EnableWriting(); // �����ݴ�д��ע��д�¼�
		task_scheduler_->UpdateChannel(channel_);
	}

	mutex_.unlock();
}

/*
�� TaskScheduler �Ƴ� channel_��ֹͣ�¼�������
ͨ�� shared_from_this() ��������ָ�룬ȷ���ص�ִ���ڼ�����
*/
void TcpConnection::Close() 
{
	if (!is_closed_) {
		is_closed_ = true;
		task_scheduler_->RemoveChannel(channel_); // ע���¼�����

		// �����û��ص����ϲ�����
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
