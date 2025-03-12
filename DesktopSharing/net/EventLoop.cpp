// PHZ
// 2019-10-18

#include "EventLoop.h"

#if defined(WIN32) || defined(_WIN32) 
#include<windows.h>
#endif

#if defined(WIN32) || defined(_WIN32) 
#pragma comment(lib, "Ws2_32.lib")
#pragma comment(lib,"Iphlpapi.lib")
#endif 

using namespace xop;

/*
���ܣ���ʼ���¼�ѭ����ָ���߳�����Ĭ��1����
�ؼ����������� Loop() �����¼�ѭ���̡߳�
*/
EventLoop::EventLoop(uint32_t num_threads)
	: index_(1)
{
	num_threads_ = 1;
	if (num_threads > 0) {
		num_threads_ = num_threads;
	}

	this->Loop();
}

/*
���ܣ���ȫֹͣ�����̲߳�������Դ��
�ؼ����������� Quit() ��ֹ������������ȴ��߳��˳���
*/
EventLoop::~EventLoop()
{
	this->Quit();
}

/*
���ܣ���ȡһ�������������
���ԣ�
���̣߳�ֱ�ӷ���Ψһ�ĵ�������
���̣߳���ѯ���ز�ͬ��������ʵ�ָ��ؾ��⡣
��;���û�ͨ���˽ӿڽ�������䵽��ͬ�̡߳�
*/
std::shared_ptr<TaskScheduler> EventLoop::GetTaskScheduler()
{
	std::lock_guard<std::mutex> locker(mutex_);
	if (task_schedulers_.size() == 1) {
		return task_schedulers_.at(0);
	}
	else {
		auto task_scheduler = task_schedulers_.at(index_);
		index_++;
		if (index_ >= task_schedulers_.size()) {
			index_ = 1;
		}		
		return task_scheduler;
	}

	return nullptr;
}

/*
���Ĺ��ܣ���ʼ�����������߳��¼�ѭ�������������������TaskScheduler����ÿ�������������ڶ����߳��У�����I/O�¼�����ʱ���ʹ�������
ƽ̨���䣺���ݲ���ϵͳѡ���Ч�Ķ�·���û��ƣ�Linux��epoll��Windows��select����
�߳����ȼ��������߳����ȼ�����Windowsʵ�֣���
*/
/*
EventLoop::Loop()
��
������ ���� (mutex_)
������ ����Ƿ��ѳ�ʼ�� �� ���򷵻�
������ ѭ�����������������Epoll/Select��
��   ������ ���� TaskScheduler ����
��   ������ �����߳�ִ�� TaskScheduler::Start()
��   ������ �洢���������߳�
������ �����߳����ȼ�����Windows��
������ ���� (mutex_)
*/
void EventLoop::Loop()
{
	std::lock_guard<std::mutex> locker(mutex_);

	if (!task_schedulers_.empty()) {
		return ;
	}

	// ����������������߳�
	/*
	ƽ̨���䣺
	Linux��ʹ�� EpollTaskScheduler������ epoll �ĸ�����I/O��·���ã���
	Windows��ʹ�� SelectTaskScheduler������ select�����ݵ�Ч�ʽϵͣ���
	����������洢����������ָ����� task_schedulers_ ������
	�߳�������Ϊÿ�������������̣߳�ִ�� TaskScheduler::Start() �����¼�ѭ����
	������룺thread->native_handle() ����ȡ�����δʹ�ã���ɾ����
	*/
	for (uint32_t n = 0; n < num_threads_; n++) 
	{
#if defined(__linux) || defined(__linux__) 
		std::shared_ptr<TaskScheduler> task_scheduler_ptr(new EpollTaskScheduler(n));
#elif defined(WIN32) || defined(_WIN32) 
		std::shared_ptr<TaskScheduler> task_scheduler_ptr(new SelectTaskScheduler(n));
#endif
		task_schedulers_.push_back(task_scheduler_ptr);
		std::shared_ptr<std::thread> thread(new std::thread(&TaskScheduler::Start, task_scheduler_ptr.get()));
		thread->native_handle();
		threads_.push_back(thread);

		// ��EventLoop���棬��һ���߳�ȥ����һ�����������
	}

	const int priority = TASK_SCHEDULER_PRIORITY_REALTIME;

	// �����߳����ȼ�����Windows��
	/*
	���ȼ�������TASK_SCHEDULER_PRIORITY_REALTIME ��Ӧ���ʵʱ���ȼ���
	Windowsʵ�֣�ͨ�� SetThreadPriority �����߳����ȼ��������ؼ�������Ӧ�ٶȡ�
	Linuxȱʧ��δʵ�����ȼ����ã��貹�� pthread ��ش��루�� pthread_setschedparam����
	*/
	for (auto iter : threads_) 
	{
#if defined(__linux) || defined(__linux__) 

#elif defined(WIN32) || defined(_WIN32) 
		switch (priority) 
		{
		case TASK_SCHEDULER_PRIORITY_LOW:
			SetThreadPriority(iter->native_handle(), THREAD_PRIORITY_BELOW_NORMAL);
			break;
		case TASK_SCHEDULER_PRIORITY_NORMAL:
			SetThreadPriority(iter->native_handle(), THREAD_PRIORITY_NORMAL);
			break;
		case TASK_SCHEDULER_PRIORITYO_HIGH:
			SetThreadPriority(iter->native_handle(), THREAD_PRIORITY_ABOVE_NORMAL);
			break;
		case TASK_SCHEDULER_PRIORITY_HIGHEST:
			SetThreadPriority(iter->native_handle(), THREAD_PRIORITY_HIGHEST);
			break;
		case TASK_SCHEDULER_PRIORITY_REALTIME:
			SetThreadPriority(iter->native_handle(), THREAD_PRIORITY_TIME_CRITICAL);
			break;
		}
#endif
	}
}

/*
���ܣ�ֹͣ�¼�ѭ����
���̣�
������������������� Stop() ��ֹ�¼�ѭ����
�ȴ������߳̽�����join()���������Դ��
*/
void EventLoop::Quit()
{
	std::lock_guard<std::mutex> locker(mutex_);

	for (auto iter : task_schedulers_) {
		iter->Stop();
	}

	for (auto iter : threads_) {
		iter->join();
	}

	task_schedulers_.clear();
	threads_.clear();
}

/*
����I/Oͨ������Socket�������ɵ�һ������������
*/
void EventLoop::UpdateChannel(ChannelPtr channel)
{
	std::lock_guard<std::mutex> locker(mutex_);
	if (task_schedulers_.size() > 0) {
		task_schedulers_[0]->UpdateChannel(channel);
	}	
}

/*
�Ƴ�I/Oͨ������Socket�������ɵ�һ������������
*/
void EventLoop::RemoveChannel(ChannelPtr& channel)
{
	std::lock_guard<std::mutex> locker(mutex_);
	if (task_schedulers_.size() > 0) {
		task_schedulers_[0]->RemoveChannel(channel);
	}	
}

/*
��Ӷ�ʱ�����ɵ�һ������������
*/
TimerId EventLoop::AddTimer(TimerEvent timerEvent, uint32_t msec)
{
	std::lock_guard<std::mutex> locker(mutex_);
	if (task_schedulers_.size() > 0) {
		return task_schedulers_[0]->AddTimer(timerEvent, msec);
	}
	return 0;
}

/*
�Ƴ���ʱ�����ɵ�һ������������
*/
void EventLoop::RemoveTimer(TimerId timerId)
{
	std::lock_guard<std::mutex> locker(mutex_);
	if (task_schedulers_.size() > 0) {
		task_schedulers_[0]->RemoveTimer(timerId);
	}	
}

/*
������������Ļص��¼�������߳�֪ͨ����
*/
bool EventLoop::AddTriggerEvent(TriggerEvent callback)
{   
	std::lock_guard<std::mutex> locker(mutex_);
	if (task_schedulers_.size() > 0) {
		return task_schedulers_[0]->AddTriggerEvent(callback);
	}
	return false;
}
