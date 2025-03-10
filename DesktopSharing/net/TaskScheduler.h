// PHZ
// 2018-5-15

#ifndef XOP_TASK_SCHEDULER_H
#define XOP_TASK_SCHEDULER_H

/*
TaskScheduler��XOP������������Ⱥ��ģ��ṩ��

ͳһ�¼�ѭ����ܣ�����I/O�¼�����ʱ������첽������
���߳�������ȣ�ͨ���ܵ��ͻ��λ�����ʵ���̼߳�ͨ�š�
��չ�ԣ������ͨ��ʵ��HandleEvent()֧�ֲ�ͬI/O��·���û��ƣ���epoll��kqueue����
����������˸����������ĵ����������¼��������첽�ص�����Դ��Ч����ʵ��ʹ������ע����Դ�ͷź��̰߳�ȫ���⡣
*/

#include "Channel.h"
#include "Pipe.h"
#include "Timer.h"
#include "RingBuffer.h"

namespace xop
{

typedef std::function<void(void)> TriggerEvent;

/*
ְ����Ϊ������ȵĻ��࣬�ṩ��ƽ̨���¼�ѭ����ܣ�����ʱ�����̼߳份�Ѻ��첽���񴥷���
���ģʽ�����Reactorģʽ���¼���������Proactorģʽ���첽���񣩣�֧�֣�
I/O�¼�������������ʵ�֣���EpollTaskScheduler����
��ʱ������ȡ�
���߳����񴥷���ͨ���ܵ����Ѻͻ��λ���������
*/
class TaskScheduler 
{
public:
	TaskScheduler(int id=1);
	virtual ~TaskScheduler();

	void Start();
	void Stop();
	TimerId AddTimer(TimerEvent timerEvent, uint32_t msec);
	void RemoveTimer(TimerId timerId);
	bool AddTriggerEvent(TriggerEvent callback);

	virtual void UpdateChannel(ChannelPtr channel) { };
	virtual void RemoveChannel(ChannelPtr& channel) { };
	virtual bool HandleEvent(int timeout) { return false; };

	int GetId() const 
	{ return id_; }

protected:
	void Wake();
	void HandleTriggerEvent();

	// ������Ψһ��ʶ�����ڶ����������������߳�ÿ���߳�һ������������
	int id_ = 0;
	// ԭ�ӱ�־λ�������¼�ѭ������ͣ��
	std::atomic_bool is_shutdown_;
	// �ܵ�����ƽ̨��װ���������̼߳份���¼�ѭ����
	std::unique_ptr<Pipe> wakeup_pipe_;
	// ����wakeup_pipe_���˵�Channel�����������¼���
	std::shared_ptr<Channel> wakeup_channel_;
	// ���λ��������洢���������첽����ص����̰߳�ȫ����
	std::unique_ptr<xop::RingBuffer<TriggerEvent>> trigger_events_;
	// ����trigger_events_���̰߳�ȫ��
	std::mutex mutex_;
	// ��ʱ�����У�����ʱ�������ӡ�ɾ����ִ�С�
	TimerQueue timer_queue_;

	static const char kTriggetEvent = 1;			// �����¼��ı�ʶ�ַ���
	static const char kTimerEvent = 2;				// ��ʱ���¼��ı�ʶ��δֱ��ʹ�ã���
	static const int  kMaxTriggetEvents = 50000;	// ���λ������������������ֹ�ڴ������
};

}
#endif  

