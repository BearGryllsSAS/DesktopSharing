#ifndef XOP_TCP_CONNECTION_H
#define XOP_TCP_CONNECTION_H

/*
TcpConnection ������� TCP ���ӵ��������ڣ��������ݶ�д�����ӹرպʹ����¼���ͨ���¼�����ģ���� TaskScheduler Эͬ������ʵ�ַ����� I/O ������

1. Acceptor ���յ������ӣ�OnAccept()��
   ������ ���� TcpConnection ����socket ���ݸ����캯��
2. TcpConnection ��ʼ��
   ������ ע�� socket ���¼��� TaskScheduler
3. ���ݵ��HandleRead()��
   ������ ��ȡ�� read_buffer_
   ������ ���� read_cb_���û���������
4. �û����� Send() ��������
   ������ д�� write_buffer_������ HandleWrite()
5. ���ӹرգ��û��������쳣��
   ������ ���� Close()��������Դ���ص�֪ͨ�ϲ�
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

	virtual void HandleRead();		// ������¼����������ݲ����� read_cb_��
	virtual void HandleWrite();		// ����д�¼������� write_buffer_ �е����ݣ��Ż� EPOLLOUT �¼�ע�ᡣ
	virtual void HandleClose();		// 
	virtual void HandleError();		// 

	void SetDisconnectCallback(const DisconnectCallback& cb)
	{ disconnect_cb_ = cb; }

	TaskScheduler* task_scheduler_;						// ��������������������¼��������߳�ִ�С�
	std::unique_ptr<xop::BufferReader> read_buffer_;	// �������������ڽ������ݡ�
	std::unique_ptr<xop::BufferWriter> write_buffer_;	// д�������������ݴ���������ݡ�
	std::atomic_bool is_closed_;						// ԭ�ӱ�������Ƿ��ѹرգ���֤�̰߳�ȫ��

private:
	void Close();								// �ر����ӣ�������Դ�������ص���

	std::shared_ptr<xop::Channel> channel_;		// ��װ socket ���¼�����������д���رա����󣩡�
	std::mutex mutex_;							// ���� write_buffer_ ��״̬������̰߳�ȫ��
	DisconnectCallback disconnect_cb_;			// ���ӶϿ�ʱ�Ļص���ͨ���� TcpServer ���ã���
	CloseCallback close_cb_;					// ���ӹر�ʱ�Ļص����û��Զ��壩��
	ReadCallback read_cb_;						// ���ݵ���ʱ�Ļص����û��Զ��壬���� false ��ر����ӣ���
};

}

#endif 
