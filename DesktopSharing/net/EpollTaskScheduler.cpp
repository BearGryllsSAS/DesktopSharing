// PHZ
// 2018-5-15

#include "EpollTaskScheduler.h"

#if defined(__linux) || defined(__linux__) 
#include <sys/epoll.h>
#include <errno.h>
#endif

using namespace xop;

/*
����epollʵ������ʼ��С1024��
ע��wakeup_channel_�������Ա���������ڿ��̻߳����¼�ѭ������
*/
EpollTaskScheduler::EpollTaskScheduler(int id)
	: TaskScheduler(id)
{
#if defined(__linux) || defined(__linux__) 
    epollfd_ = epoll_create(1024);
 #endif
    this->UpdateChannel(wakeup_channel_);
}

EpollTaskScheduler::~EpollTaskScheduler()
{
	
}

/*
���ܣ���ӻ����Channel���¼�������
�߼���
��Channel�Ѵ��ڣ�
���¼�ΪEVENT_NONE����epoll��channels_��ɾ����
���򣬸���epoll�����¼���
��Channel���������й�ע���¼�����ӵ�epoll��channels_��
*/
void EpollTaskScheduler::UpdateChannel(ChannelPtr channel)
{
	std::lock_guard<std::mutex> lock(mutex_);
#if defined(__linux) || defined(__linux__) 
	int fd = channel->GetSocket();
	if(channels_.find(fd) != channels_.end()) {
		if(channel->IsNoneEvent()) {
			Update(EPOLL_CTL_DEL, channel);
			channels_.erase(fd);
		}
		else {
			Update(EPOLL_CTL_MOD, channel);
		}
	}
	else {
		if(!channel->IsNoneEvent()) {
			channels_.emplace(fd, channel);
			Update(EPOLL_CTL_ADD, channel);
		}	
	}	
#endif
}

/*
���ܣ���װepoll_ctlϵͳ���ã���epollʵ��ע��/�޸�/ɾ���¼���
�ؼ��㣺
event.data.ptr�洢Channel�����ԭʼָ�룬�����¼�����ʱ���ٶ�λ�������
δ����epoll_ctl�Ĵ�������־��¼���쳣�׳�����
*/
void EpollTaskScheduler::Update(int operation, ChannelPtr& channel)
{
#if defined(__linux) || defined(__linux__) 
	struct epoll_event event = {0};

	if(operation != EPOLL_CTL_DEL) {
		event.data.ptr = channel.get();
		event.events = channel->GetEvents();
	}

	if(::epoll_ctl(epollfd_, operation, channel->GetSocket(), &event) < 0) {

	}
#endif
}

/*
���ܣ���epoll��channels_���Ƴ�ָ��Channel��
��;�������ӹرջ�����Ҫ�����¼�ʱ���á�
*/
void EpollTaskScheduler::RemoveChannel(ChannelPtr& channel)
{
    std::lock_guard<std::mutex> lock(mutex_);
#if defined(__linux) || defined(__linux__) 
	int fd = channel->GetSocket();

	if(channels_.find(fd) != channels_.end()) {
		Update(EPOLL_CTL_DEL, channel);
		channels_.erase(fd);
	}
#endif
}

/*
���ܣ������¼�ѭ���������ȴ��¼�������
���̣�
����epoll_wait�ȴ��¼�����ʱʱ���ɲ���ָ�������룩��
���������¼���ͨ��event.data.ptr��ȡ������Channel����
����Channel::HandleEvent()��������¼��������д�����󣩡�
�ؼ��㣺
ÿ����ദ��512���¼���events�����С����
�����ź��жϣ�EINTR��ʱ�������ء�
*/
bool EpollTaskScheduler::HandleEvent(int timeout)
{
#if defined(__linux) || defined(__linux__) 
	struct epoll_event events[512] = {0};
	int num_events = -1;

	num_events = epoll_wait(epollfd_, events, 512, timeout);
	if(num_events < 0)  {
		if(errno != EINTR) {	
			return false;	// �����ź��ж�
		}								
	}

	for(int n=0; n<num_events; n++) {
		if(events[n].data.ptr) {        
			((Channel *)events[n].data.ptr)->HandleEvent(events[n].events);
		}
	}		
	return true;
#else
    return false;
#endif
}


