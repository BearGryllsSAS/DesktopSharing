#include "TaskScheduler.h"
#if defined(__linux) || defined(__linux__) 
#include <signal.h>
#endif

using namespace xop;

/*
跨平台初始化：Windows下初始化Winsock。
创建管道：用于线程间唤醒。
设置唤醒Channel：监听管道读端，触发时调用Wake()清空管道。
*/
TaskScheduler::TaskScheduler(int id)
	: id_(id)
	, is_shutdown_(false) 
	, wakeup_pipe_(new Pipe())
	, trigger_events_(new xop::RingBuffer<TriggerEvent>(kMaxTriggetEvents))
{
	// Windows网络库初始化（一次）
	static std::once_flag flag;
	std::call_once(flag, [] {
#if defined(WIN32) || defined(_WIN32) 
		WSADATA wsa_data;
		if (WSAStartup(MAKEWORD(2, 2), &wsa_data)) {
			WSACleanup();
		}
#endif
	});

	// 创建唤醒管道并设置Channel
	// wakeup_pipe_ 是 TaskScheduler 类中 std::unique_ptr<Pipe> 类型的成员变量，Pipe 维护了一个 SOCKET pipe_fd_[2] 类型的管道。wakeup_pipe_->Create() 作用是创建和初始化这两个管道
	if (wakeup_pipe_->Create()) {
		// 把管道的读端 channel 化，然后设置读端的读事件的回调函数
		wakeup_channel_.reset(new Channel(wakeup_pipe_->Read()));
		wakeup_channel_->EnableReading();
		wakeup_channel_->SetReadCallback([this]() { this->Wake(); });		
	}        
}

TaskScheduler::~TaskScheduler()
{
	
}

/*
处理异步任务（trigger_events_中的回调）。
处理到期的定时器任务。
调用子类实现的HandleEvent()监听I/O事件，超时时间由最近定时器决定。

?????????? 这一块有点不太懂它的逻辑，但是又是很重要的一个任务调度函数
*/
void TaskScheduler::Start()
{
#if defined(__linux) || defined(__linux__) 
	// 忽略信号
	signal(SIGPIPE, SIG_IGN);
	signal(SIGQUIT, SIG_IGN);
	signal(SIGUSR1, SIG_IGN);
	signal(SIGTERM, SIG_IGN);
	signal(SIGKILL, SIG_IGN);
#endif     
	is_shutdown_ = false;
	while (!is_shutdown_) {
		this->HandleTriggerEvent();					// 处理异步任务
		this->timer_queue_.HandleTimerEvent();		// 处理定时任务
		int64_t timeout = this->timer_queue_.GetTimeRemaining();
		this->HandleEvent((int)timeout);			// 子类实现，如epoll_wait
	}
}

void TaskScheduler::Stop()
{
	is_shutdown_ = true;
	char event = kTriggetEvent;
	wakeup_pipe_->Write(&event, 1);	// 写入唤醒字符，退出事件循环
}

/*
定时器管理
委托给timer_queue_：具体实现由TimerQueue类处理，支持添加和删除定时器。
*/
TimerId TaskScheduler::AddTimer(TimerEvent timerEvent, uint32_t msec)
{
	TimerId id = timer_queue_.AddTimer(timerEvent, msec);
	return id;
}

/*
定时器管理
委托给timer_queue_：具体实现由TimerQueue类处理，支持添加和删除定时器。
*/
void TaskScheduler::RemoveTimer(TimerId timerId)
{
	timer_queue_.RemoveTimer(timerId);
}

/*
将回调存入环形缓冲区。
向管道写入数据，唤醒可能阻塞在HandleEvent()中的事件循环。
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
作用：当管道有数据可读时（被唤醒），读取并丢弃数据，避免重复触发。
*/
void TaskScheduler::Wake()
{
	char event[10] = { 0 };
	while (wakeup_pipe_->Read(event, 10) > 0);
}

/*
消费回调：从环形缓冲区取出所有任务并执行。
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
