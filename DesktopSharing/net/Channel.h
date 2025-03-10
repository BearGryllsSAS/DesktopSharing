// PHZ
// 2018-5-15
    
#ifndef XOP_CHANNEL_H
#define XOP_CHANNEL_H

/*
Channel����XOP�����ĺ������֮һ������Socket���¼�����봦���߼����ͨ���ص������ṩ�����¼��������ģ�͡�
����Ƽ���Ч���ʺϹ��������������������
*/

#include <functional>
#include <memory>
#include "Socket.h"

namespace xop
{

/*
���ã�����Socket���ܴ������¼����ͣ�ʹ��λ������ϣ���EVENT_IN | EVENT_OUT����
��ƣ�ֵ��Linux��epoll�¼�����EPOLLIN��EPOLLOUT����Ӧ�������˿�ƽ̨����
*/
enum EventType {
	EVENT_NONE = 0,     // ���¼�
	EVENT_IN = 1,		// �ɶ��¼��������ݵ��
	EVENT_PRI = 2,		// �����ȼ����ݣ���������ݣ�
	EVENT_OUT = 4,		// ��д�¼�
	EVENT_ERR = 8,		// �����¼�
	EVENT_HUP = 16,		// ���ӹ�����Զ˹رգ�
	EVENT_RDHUP = 8192	// �Զ˹ر����ӣ�EPOLLRDHUP�ķ�װ��
};

/*
ְ�𣺷�װһ��Socket�ļ����������������ע���¼�������д������ȣ�����ͨ���ص����������¼���
���ģʽ������Reactorģʽ�����¼�����봦���߼����
*/
class Channel 
{
public:
	typedef std::function<void()> EventCallback;
    
	// ǿ��ͨ��Socket���죬ȷ��ÿ��Channel�����Ӧһ����Ч��Socket��
	Channel() = delete;

	Channel(SOCKET sockfd) 
		: sockfd_(sockfd)
	{

	}

	virtual ~Channel() {};
    
	// �����û�Ϊ��ͬ�¼�ע���Զ��崦���߼���
	void SetReadCallback(const EventCallback& cb)
	{ read_callback_ = cb; }

	void SetWriteCallback(const EventCallback& cb)
	{ write_callback_ = cb; }

	void SetCloseCallback(const EventCallback& cb)
	{ close_callback_ = cb; }

	void SetErrorCallback(const EventCallback& cb)
	{ error_callback_ = cb; }

	SOCKET GetSocket() const { return sockfd_; }

	int  GetEvents() const { return events_; }
	void SetEvents(int events) { events_ = events; }
    
	// ͨ��λ�����޸�events_�������¼�����״̬��
	void EnableReading() 
	{ events_ |= EVENT_IN; }

	void EnableWriting() 
	{ events_ |= EVENT_OUT; }
    
	void DisableReading() 
	{ events_ &= ~EVENT_IN; }
    
	void DisableWriting() 
	{ events_ &= ~EVENT_OUT; }
       
	// �жϵ�ǰ�Ƿ��ע�ض��¼���
	bool IsNoneEvent() const { return events_ == EVENT_NONE; }
	bool IsWriting() const { return (events_ & EVENT_OUT) != 0; }
	bool IsReading() const { return (events_ & EVENT_IN) != 0; }
    
	// ���ݴ�����¼����ʹ�����Ӧ�Ļص���
	// ע�⣺EVENT_HUP������ֱ�ӷ��أ��������������¼�����ͬʱ������EVENT_ERR����
	void HandleEvent(int events)
	{	
		if (events & (EVENT_PRI | EVENT_IN)) {
			read_callback_();
		}

		if (events & EVENT_OUT) {
			write_callback_();
		}
        
		if (events & EVENT_HUP) {
			close_callback_();
			return ;
		}

		if (events & (EVENT_ERR)) {
			error_callback_();
		}
	}

private:
	EventCallback read_callback_ = [] {};	// ���¼��ص�
	EventCallback write_callback_ = [] {};  // д�¼��ص�
	EventCallback close_callback_ = [] {};  // �ر��¼��ص�
	EventCallback error_callback_ = [] {};  // �����¼��ص�
	
	SOCKET sockfd_ = 0;						// ������Socket���
	int events_ = 0;						// ��ǰ��ע���¼���λ���룩		��EVENT_IN | EVENT_OUT��
};

typedef std::shared_ptr<Channel> ChannelPtr;

}

#endif  

