// PHZ
// 2018-5-15

#ifndef XOP_EVENT_LOOP_H
#define XOP_EVENT_LOOP_H

/*
ʵ����һ����ƽ̨���¼�ѭ����EventLoop�������ڹ����첽I/O�¼�����ʱ���ʹ����¼�

���Ļ��ƣ�ͨ�����߳��������������I/O����ʱ���ʹ����¼���֧�ֿ�ƽ̨��
ʹ�ó�������������Ҫ�߲���I/O������������������Ƶ������������
ע����������ʵ����������߳��������ȼ������ⵥ���������ء�
*/

#include <memory>
#include <atomic>
#include <unordered_map>
#include <functional>
#include <queue>
#include <thread>
#include <mutex>

#include "SelectTaskScheduler.h"
#include "EpollTaskScheduler.h"
#include "Pipe.h"
#include "Timer.h"
#include "RingBuffer.h"

#define TASK_SCHEDULER_PRIORITY_LOW       0
#define TASK_SCHEDULER_PRIORITY_NORMAL    1
#define TASK_SCHEDULER_PRIORITYO_HIGH     2 
#define TASK_SCHEDULER_PRIORITY_HIGHEST   3
#define TASK_SCHEDULER_PRIORITY_REALTIME  4

namespace xop
{

/*
���߳�������ȣ�֧�ֶ�������������TaskScheduler����ÿ�������������ڶ����߳��У�ͨ����ѯ���Է������������������ܡ�

��ƽ̨֧�֣�
Linux��ʹ��EpollTaskScheduler������epoll�ĸ�ЧI/O��·���ã���
Windows��ʹ��SelectTaskScheduler������select�������ԽϺõ�Ч�ʽϵͣ���

���ȼ��������������߳����ȼ���Windowsƽ̨����֧��ʵʱ������
*/
class EventLoop 
{
public:
	EventLoop(const EventLoop&) = delete;
	EventLoop &operator = (const EventLoop&) = delete; 
	EventLoop(uint32_t num_threads =1);  //std::thread::hardware_concurrency()
	virtual ~EventLoop();

	std::shared_ptr<TaskScheduler> GetTaskScheduler();

	bool AddTriggerEvent(TriggerEvent callback);
	TimerId AddTimer(TimerEvent timerEvent, uint32_t msec);
	void RemoveTimer(TimerId timerId);	
	void UpdateChannel(ChannelPtr channel);
	void RemoveChannel(ChannelPtr& channel);
	
	void Loop();
	void Quit();

private:
	std::mutex mutex_;			// �������������Թ�����Դ��������������б��߳��б��Ĳ������ʡ�
	uint32_t num_threads_ = 1;	// �¼�ѭ�����߳�����Ĭ��1����ͨ�����캯��ָ����
	uint32_t index_ = 1;		// ��ѯ�����������ڶ��̳߳����¾�����������������
	std::vector<std::shared_ptr<TaskScheduler>> task_schedulers_;	// �洢���������������������ÿ������������һ���¼�ѭ���̡߳�
	std::vector<std::shared_ptr<std::thread>> threads_;				// �洢�����̶߳����������ÿ���߳�����һ�������������
};

}

#endif