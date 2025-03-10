// PHZ
// 2018-5-15

#include "SelectTaskScheduler.h"
#include "Logger.h"
#include "Timer.h"
#include <cstring>
#include <forward_list>

using namespace xop;

#define SELECT_CTL_ADD	0
#define SELECT_CTL_MOD  1
#define SELECT_CTL_DEL	2

/*
初始化fd_set备份集合。
注册基类的wakeup_channel_（用于跨线程唤醒事件循环）。
*/
SelectTaskScheduler::SelectTaskScheduler(int id) : TaskScheduler(id) 
{
	FD_ZERO(&fd_read_backup_); // 清空fd_set
	FD_ZERO(&fd_write_backup_);
	FD_ZERO(&fd_exp_backup_);
	// 把在 TaskScheduler(id) 中已经 channel 化的管道读端添加到 SelectTaskScheduler 类成员变量 std::unordered_map<SOCKET, ChannelPtr> channels_ 中
	this->UpdateChannel(wakeup_channel_); // 注册唤醒Channel
}

SelectTaskScheduler::~SelectTaskScheduler()
{
	
}

/*
删除Channel：设置所有重置标志，确保下次select更新fd_set。
修改Channel：仅设置写事件重置标志（可能优化，避免全量更新）。
新增Channel：设置所有重置标志，并添加到channels_。
*/
void SelectTaskScheduler::UpdateChannel(ChannelPtr channel) 
{
	std::lock_guard<std::mutex> lock(mutex_);
	SOCKET socket = channel->GetSocket();

	if (channels_.find(socket) != channels_.end()) {
		if (channel->IsNoneEvent()) { // 删除Channel
			is_fd_read_reset_ = true;
			is_fd_write_reset_ = true;
			is_fd_exp_reset_ = true;
			channels_.erase(socket);
		}
		else { // 修改事件
			is_fd_write_reset_ = true; // 仅写事件需要重置？
		}
	}
	else { // 新增Channel
		if (!channel->IsNoneEvent()) {
			channels_.emplace(socket, channel);
			is_fd_read_reset_ = is_fd_write_reset_ = is_fd_exp_reset_ = true;
		}
	}
}

/*
移除Channel并标记需要重置fd_set。
*/
void SelectTaskScheduler::RemoveChannel(ChannelPtr& channel)
{
	std::lock_guard<std::mutex> lock(mutex_);

	SOCKET fd = channel->GetSocket();

	if(channels_.find(fd) != channels_.end()) {
		is_fd_read_reset_ = true;
		is_fd_write_reset_ = true;
		is_fd_exp_reset_ = true;
		channels_.erase(fd);
	}
}

/*
构建fd_set：根据重置标志决定是否重新遍历channels_构建新的集合。
调用select：等待事件发生，超时时间由最近定时器决定。
处理事件：遍历所有Channel，检查哪些Socket触发了事件，收集后调用HandleEvent。
*/
/*
​输入：超时时间 timeout（单位：毫秒）。
​输出：true 表示成功处理，false 表示错误。
​功能：监控所有注册的通道（channels_）的 I/O 事件（可读、可写、异常），触发对应回调。
*/
bool SelectTaskScheduler::HandleEvent(int timeout)
{	
	/*
	场景：当 channels_ 中没有任何要监听的描述符时，直接休眠指定时间（避免 CPU 空转）。
​	意义：优化资源使用，防止无事件时忙等待。
	*/
	if(channels_.empty()) {
		if (timeout <= 0) {
			timeout = 10; // 默认休眠 10ms
		}
         
		Timer::Sleep(timeout);
		return true;
	}

	// 初始化三个 fd_set 结构体，分别对应可读、可写、异常事件。
	fd_set fd_read;
	fd_set fd_write;
	fd_set fd_exp;
	FD_ZERO(&fd_read);
	FD_ZERO(&fd_write);
	FD_ZERO(&fd_exp);


	bool fd_read_reset = false;
	bool fd_write_reset = false;
	bool fd_exp_reset = false;

	if(is_fd_read_reset_ || is_fd_write_reset_ || is_fd_exp_reset_) {
		if (is_fd_exp_reset_) {
			maxfd_ = 0;
		}
          
		std::lock_guard<std::mutex> lock(mutex_);
		for(auto iter : channels_) {
			int events = iter.second->GetEvents();
			SOCKET fd = iter.second->GetSocket();

			if (is_fd_read_reset_ && (events&EVENT_IN)) {
				FD_SET(fd, &fd_read);
			}

			if(is_fd_write_reset_ && (events&EVENT_OUT)) {
				FD_SET(fd, &fd_write);
			}

			if(is_fd_exp_reset_) {
				FD_SET(fd, &fd_exp);
				if(fd > maxfd_) {
					maxfd_ = fd;
				}
			}		
		}
        
		fd_read_reset = is_fd_read_reset_;
		fd_write_reset = is_fd_write_reset_;
		fd_exp_reset = is_fd_exp_reset_;
		is_fd_read_reset_ = false;
		is_fd_write_reset_ = false;
		is_fd_exp_reset_ = false;
	}
	
	if(fd_read_reset) {
		FD_ZERO(&fd_read_backup_);
		memcpy(&fd_read_backup_, &fd_read, sizeof(fd_set)); 
	}
	else {
		memcpy(&fd_read, &fd_read_backup_, sizeof(fd_set));
	}
       

	if(fd_write_reset) {
		FD_ZERO(&fd_write_backup_);
		memcpy(&fd_write_backup_, &fd_write, sizeof(fd_set));
	}
	else {
		memcpy(&fd_write, &fd_write_backup_, sizeof(fd_set));
	}
     

	if(fd_exp_reset) {
		FD_ZERO(&fd_exp_backup_);
		memcpy(&fd_exp_backup_, &fd_exp, sizeof(fd_set));
	}
	else {
		memcpy(&fd_exp, &fd_exp_backup_, sizeof(fd_set));
	}

	if(timeout <= 0) {
		timeout = 10;
	}

	struct timeval tv = { timeout/1000, timeout%1000*1000 };
	int ret = select((int)maxfd_+1, &fd_read, &fd_write, &fd_exp, &tv); 	
	if (ret < 0) {
#if defined(__linux) || defined(__linux__) 
	if(errno == EINTR) {
		return true;
	}					
#endif 
		return false;
	}

	std::forward_list<std::pair<ChannelPtr, int>> event_list;
	if(ret > 0) {
		std::lock_guard<std::mutex> lock(mutex_);
		for(auto iter : channels_) {
			int events = 0;
			SOCKET socket = iter.second->GetSocket();

			if (FD_ISSET(socket, &fd_read)) {
				events |= EVENT_IN;
			}

			if (FD_ISSET(socket, &fd_write)) {
				events |= EVENT_OUT;
			}

			if (FD_ISSET(socket, &fd_exp)) {
				events |= (EVENT_HUP); // close
			}

			if(events != 0) {
				event_list.emplace_front(iter.second, events);
			}
		}
	}	

	for(auto& iter: event_list) {
		iter.first->HandleEvent(iter.second);
	}

	return true;
}



