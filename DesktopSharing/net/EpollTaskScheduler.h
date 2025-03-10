// PHZ
// 2018-5-15

#ifndef XOP_EPOLL_TASK_SCHEDULER_H
#define XOP_EPOLL_TASK_SCHEDULER_H

/*
EpollTaskScheduler��XOP������л���Linux epollʵ�ֵ��¼������������Ĺ��ܰ�����

������Channel���¼�ע����ɾ����
ͨ��epoll_wait�����¼�����Ч�ַ�����Ӧ��Channel����
�ṩ�̰߳�ȫ��Channel���½ӿڡ�
����Ƽ���Ч������ע����Դ����ʹ���������ơ���ʵ��ʹ���У�ͨ����Ϊ������������¼�ѭ��������������Channelʵ�ָ������첽I/O��
*/

#include "TaskScheduler.h"
#include <mutex>
#include <unordered_map>

namespace xop
{
/*
ְ�𣺻���Linux��epollʵ�ֵĸ�ЧI/O�¼��������������̳���TaskScheduler�����������Channel���¼�������ַ���
���ģʽ��Reactorģʽ��ͨ��epollʵ�ֶ�·���ã�����Socket�¼���������Ӧ��Channel�ص���
*/
class EpollTaskScheduler : public TaskScheduler
{
public:
	EpollTaskScheduler(int id = 0);
	virtual ~EpollTaskScheduler();

	void UpdateChannel(ChannelPtr channel);
	void RemoveChannel(ChannelPtr& channel);

	// timeout: ms
	bool HandleEvent(int timeout);

private:
	void Update(int operation, ChannelPtr& channel);

	int epollfd_ = -1;
	std::mutex mutex_;
	std::unordered_map<int, ChannelPtr> channels_;	// ά��Socket�ļ���������int����Channel�����ӳ�䣬���ڿ��ٲ��Һ͹�������ע���Channel��
};

}

#endif
