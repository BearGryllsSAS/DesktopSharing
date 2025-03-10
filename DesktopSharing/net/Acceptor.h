#ifndef XOP_ACCEPTOR_H
#define XOP_ACCEPTOR_H

/*
Acceptor ������ʵ�� TCP ��������������������׽��֣�SOCKET����ͨ���¼�ѭ����EventLoop������ͻ����������󣬲�ͨ���ص����ƽ������Ӵ��ݸ��ϲ�ģ�顣

1. Listen() ��ʼ���׽��ֲ�ע���¼�����
   ������ EventLoop ����׽��ֿɶ��¼�
2. �����ӵ��ﴥ�� OnAccept()
   ������ ���� accept() ��ȡ���׽���
   ������ ͨ���ص������׽��ָ��ϲ㴦��
3. Close() ע���¼��������ͷ���Դ
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

	// ���������ӻص�������
	void SetNewConnectionCallback(const NewConnectionCallback& cb)
	{ new_connection_callback_ = cb; }

	// ��ʼ�������׽��֣��󶨶˿ڣ�ע���¼��� EventLoop��
	int  Listen(std::string ip, uint16_t port);
	// �رռ����׽��֣��� EventLoop ע���¼�������
	void Close();

private:
	// �������������󣬴����ص�������
	void OnAccept();

	EventLoop* event_loop_ = nullptr;				// ָ���¼�ѭ����ָ�룬����ע��/ע�������¼���
	std::mutex mutex_;								// ������������ tcp_socket_ �� channel_ptr_ ���̰߳�ȫ���ʡ�
	std::unique_ptr<TcpSocket> tcp_socket_;			// ��װ�����׽��֣��������������ڣ��Զ��ͷ���Դ����
	ChannelPtr channel_ptr_;						// ��װ�׽��ֵ��¼���������ɶ��¼������󶨻ص����� OnAccept()��
	NewConnectionCallback new_connection_callback_;	// �����ӵ���ʱ�Ļص�����������Ϊ�����ӵ��׽��֣�SOCKET����
};

}

#endif 
