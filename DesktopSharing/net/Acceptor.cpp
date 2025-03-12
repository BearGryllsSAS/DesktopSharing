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
�رվ��׽��֣�ȷ��ÿ�ε��� Listen() ʱ���³�ʼ����
�����׽������ԣ�
ReuseAddr/ReusePort������ TIME_WAIT ״̬���°�ʧ�ܡ�
NonBlock��������ģʽ����ֹ accept() �����¼�ѭ����
������������� TcpSocket �� Bind() �� Listen()��
ע���¼���
���� Channel ���󣬰󶨶��¼��ص� OnAccept()��
���ö��¼���EnableReading()������ Channel ע�ᵽ EventLoop��
*/
int Acceptor::Listen(std::string ip, uint16_t port) {
    std::lock_guard<std::mutex> locker(mutex_);

    // 1. �رվ��׽��֣�������ڣ�
    if (tcp_socket_->GetSocket() > 0) {
        tcp_socket_->Close();
    }

    // 2. �������׽��ֲ���������
    SOCKET sockfd = tcp_socket_->Create();
    SocketUtil::SetReuseAddr(sockfd);  // �����ַ����
    SocketUtil::SetReusePort(sockfd);  // ����˿ڸ���
    SocketUtil::SetNonBlock(sockfd);   // ������ģʽ

    // 3. �󶨲������˿�
    if (!tcp_socket_->Bind(ip, port)) return -1;
    if (!tcp_socket_->Listen(1024)) return -1;

    // 4. ע���¼�����
    channel_ptr_.reset(new Channel(sockfd)); // �Ѽ��� sockfd ���� Channel ���󣬸�ֵ�� Acceptor ���е� Channel ���͵�����ָ���Ա����
    channel_ptr_->SetReadCallback([this]() { this->OnAccept(); }); // ������ sockfd �ж��¼��������¼�����ʱ�򣬵��� void Acceptor::OnAccept() �����µĿͻ�������
    channel_ptr_->EnableReading();
    // ����select����
    event_loop_->UpdateChannel(channel_ptr_); // event_loop_ �� Acceptor ���� EventLoop* ���͵ĳ�Ա������EventLoop ��װ���߳����������������������е��������ڵ������д���ģ�
   
    /*
    void EventLoop::UpdateChannel(ChannelPtr channel)
    {
	    std::lock_guard<std::mutex> locker(mutex_);
	    if (task_schedulers_.size() > 0) {
	    	task_schedulers_[0]->UpdateChannel(channel); // ����� void SelectTaskScheduler::UpdateChannel(ChannelPtr channel) 
                                                        // ����ִ�� channels_.emplace(socket, channel)
                                                        // channels_ ����Ϊ std::unordered_map<SOCKET, ChannelPtr> channels_;
	    }	
    }
    */
    return 0;
}

/*
��Դ�ͷţ��� EventLoop �Ƴ� Channel���ر��׽��֡�
*/
void Acceptor::Close() {
    std::lock_guard<std::mutex> locker(mutex_);

    if (tcp_socket_->GetSocket() > 0) {
        event_loop_->RemoveChannel(channel_ptr_); // ע���¼�����
        tcp_socket_->Close();                     // �ر��׽���
    }
}

/*
���ܣ��������׽��ֿɶ�ʱ���������ӣ������� accept() �������ӡ�
�̰߳�ȫ��ͨ�� mutex_ ���� tcp_socket_ �� Accept() ������
�ص������������˻ص��������׽��ִ��ݸ��ϲ㣻����ֱ�ӹرա�
*/
void Acceptor::OnAccept() {
    std::lock_guard<std::mutex> locker(mutex_);

    // tcp_socket_ �� Acceptor ���� TcpSocket ���͵ĳ�Ա���������ĳ�Ա���� sockfd_ �� int Acceptor::Listen(std::string ip, uint16_t port) ���Ѿ�����Ϊ�����׽���
    SOCKET socket = tcp_socket_->Accept();
    if (socket > 0) {
        if (new_connection_callback_) {
            new_connection_callback_(socket); // �����������׽���
        }
        else {
            SocketUtil::Close(socket);        // �޻ص���ر�����
        }
    }
}

