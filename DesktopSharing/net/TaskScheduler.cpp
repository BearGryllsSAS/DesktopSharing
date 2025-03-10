#include "TaskScheduler.h"
#if defined(__linux) || defined(__linux__) 
#include <signal.h>
#endif

using namespace xop;

/*
��ƽ̨��ʼ����Windows�³�ʼ��Winsock��
�����ܵ��������̼߳份�ѡ�
���û���Channel�������ܵ����ˣ�����ʱ����Wake()��չܵ���
*/
TaskScheduler::TaskScheduler(int id)
	: id_(id)
	, is_shutdown_(false) 
	, wakeup_pipe_(new Pipe())
	, trigger_events_(new xop::RingBuffer<TriggerEvent>(kMaxTriggetEvents))
{
	// Windows������ʼ����һ�Σ�
	static std::once_flag flag;
	std::call_once(flag, [] {
#if defined(WIN32) || defined(_WIN32) 
		WSADATA wsa_data;
		if (WSAStartup(MAKEWORD(2, 2), &wsa_data)) {
			WSACleanup();
		}
#endif
	});

	// �������ѹܵ�������Channel
	// wakeup_pipe_ �� TaskScheduler ���� std::unique_ptr<Pipe> ���͵ĳ�Ա������Pipe ά����һ�� SOCKET pipe_fd_[2] ���͵Ĺܵ���wakeup_pipe_->Create() �����Ǵ����ͳ�ʼ���������ܵ�
	if (wakeup_pipe_->Create()) {
		// �ѹܵ��Ķ��� channel ����Ȼ�����ö��˵Ķ��¼��Ļص�����
		wakeup_channel_.reset(new Channel(wakeup_pipe_->Read()));
		wakeup_channel_->EnableReading();
		wakeup_channel_->SetReadCallback([this]() { this->Wake(); });		
	}        
}

TaskScheduler::~TaskScheduler()
{
	
}

/*
�����첽����trigger_events_�еĻص�����
�����ڵĶ�ʱ������
��������ʵ�ֵ�HandleEvent()����I/O�¼�����ʱʱ���������ʱ��������

?????????? ��һ���е㲻̫�������߼����������Ǻ���Ҫ��һ��������Ⱥ���
*/
void TaskScheduler::Start()
{
#if defined(__linux) || defined(__linux__) 
	// �����ź�
	signal(SIGPIPE, SIG_IGN);
	signal(SIGQUIT, SIG_IGN);
	signal(SIGUSR1, SIG_IGN);
	signal(SIGTERM, SIG_IGN);
	signal(SIGKILL, SIG_IGN);
#endif     
	is_shutdown_ = false;
	while (!is_shutdown_) {
		this->HandleTriggerEvent();					// �����첽����
		this->timer_queue_.HandleTimerEvent();		// ����ʱ����
		int64_t timeout = this->timer_queue_.GetTimeRemaining();
		this->HandleEvent((int)timeout);			// ����ʵ�֣���epoll_wait
	}
}

void TaskScheduler::Stop()
{
	is_shutdown_ = true;
	char event = kTriggetEvent;
	wakeup_pipe_->Write(&event, 1);	// д�뻽���ַ����˳��¼�ѭ��
}

/*
��ʱ������
ί�и�timer_queue_������ʵ����TimerQueue�ദ��֧����Ӻ�ɾ����ʱ����
*/
TimerId TaskScheduler::AddTimer(TimerEvent timerEvent, uint32_t msec)
{
	TimerId id = timer_queue_.AddTimer(timerEvent, msec);
	return id;
}

/*
��ʱ������
ί�и�timer_queue_������ʵ����TimerQueue�ദ��֧����Ӻ�ɾ����ʱ����
*/
void TaskScheduler::RemoveTimer(TimerId timerId)
{
	timer_queue_.RemoveTimer(timerId);
}

/*
���ص����뻷�λ�������
��ܵ�д�����ݣ����ѿ���������HandleEvent()�е��¼�ѭ����
*/
bool TaskScheduler::AddTriggerEvent(TriggerEvent callback)
{
	if (trigger_events_->Size() < kMaxTriggetEvents) {
		std::lock_guard<std::mutex> lock(mutex_);
		char event = kTriggetEvent;
		trigger_events_->Push(std::move(callback));
		wakeup_pipe_->Write(&event, 1);
		return true;
	}

	return false;
}

/*
���ã����ܵ������ݿɶ�ʱ�������ѣ�����ȡ���������ݣ������ظ�������
*/
void TaskScheduler::Wake()
{
	char event[10] = { 0 };
	while (wakeup_pipe_->Read(event, 10) > 0);
}

/*
���ѻص����ӻ��λ�����ȡ����������ִ�С�
*/
void TaskScheduler::HandleTriggerEvent()
{
	do 
	{
		TriggerEvent callback;
		if (trigger_events_->Pop(callback)) {
			callback();
		}
	} while (trigger_events_->Size() > 0);
}
