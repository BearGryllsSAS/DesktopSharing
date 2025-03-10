// PHZ
// 2018-11-10

#ifndef XOP_TCPSERVER_H
#define XOP_TCPSERVER_H

/*
TcpServer ����һ�� TCP ����������������˿ڡ����ܿͻ������ӣ����������л�Ծ�� TcpConnection ������������Χ�� �¼�ѭ�������ӹ��� �� �̰߳�ȫ չ����

1. �û����� Start(ip, port)
   ������ Acceptor ��ʼ����
2. �����ӵ���
   ������ Acceptor �����ص������� TcpConnection
   ������ TcpConnection ע�ᵽ EventLoop
   ������ ���Ӽ��� connections_ ӳ��
3. ���ݵ���/����
   ������ TcpConnection �� HandleRead/HandleWrite ����
4. ���ӹر�
   ������ ���� DisconnectCallback���� connections_ �Ƴ�
5. �û����� Stop()
   ������ �ر� Acceptor���Ͽ��������ӣ��ȴ���Դ�ͷ�
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

	// ��������������ʼ����ָ�� IP �Ͷ˿ڡ�
	virtual bool Start(std::string ip, uint16_t port);
	// ֹͣ���������ر��������Ӳ��ͷ���Դ��
	virtual void Stop();

	std::string GetIPAddress() const
	{ return ip_; }

	uint16_t GetPort() const 
	{ return port_; }

protected:
	// �麯�������� TcpConnection ���󣨿ɱ����า��ʵ���Զ��������߼�����
	virtual TcpConnection::Ptr OnConnect(SOCKET sockfd);
	// ����������ӵ� connections_ ӳ�䡣
	virtual void AddConnection(SOCKET sockfd, TcpConnection::Ptr tcp_conn);
	// �� connections_ �Ƴ�ָ�����ӡ�
	virtual void RemoveConnection(SOCKET sockfd);

	EventLoop* event_loop_;					// �¼�ѭ���������� Acceptor ���������ӵ� I/O �¼�����
	uint16_t port_;
	std::string ip_;
	std::unique_ptr<Acceptor> acceptor_;	// ��������������������ӡ�
	bool is_started_;
	std::mutex mutex_;						// ���� connections_ ���̰߳�ȫ���ʡ�
	std::unordered_map<SOCKET, TcpConnection::Ptr> connections_;	// �洢���л�Ծ���ӣ���Ϊ�׽�����������
};

}

#endif 
