// PHZ
// 2018-5-15

#ifndef XOP_SELECT_TASK_SCHEDULER_H
#define XOP_SELECT_TASK_SCHEDULER_H

/*
SelectTaskScheduler��XOP������л���select�Ŀ�ƽ̨�¼��������������ص������

�����ԣ�֧��Linux/Windows���ʺϵͲ���������
�¼�����ͨ������fd_set�Ż����ܣ������ظ�����������
�̰߳�ȫ��ʹ�û���������channels_��ȷ��������ȫ��
�����Ȩ��������������ԣ���EpollTaskScheduler�Ĳ��䷽������ʵ��Ӧ���У�����ݳ���ѡ����������߲�����epoll����ƽ̨��Ͳ�����select��
*/

#include "TaskScheduler.h"
#include "Socket.h"
#include <mutex>
#include <unordered_map>

#if defined(__linux) || defined(__linux__) 
#include <sys/select.h>
#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>
#endif

namespace xop
{	

/*
ְ�𣺻���selectϵͳ����ʵ�ֿ�ƽ̨���¼��������������̳���TaskScheduler��������Channel���¼�������ַ���
���ó������Ͳ�������Ҫ��ƽ̨����Windows���ĳ��������EpollTaskScheduler�����ܽϵ͵������Ը��㡣
*/
class SelectTaskScheduler : public TaskScheduler
{
public:
	SelectTaskScheduler(int id = 0);
	virtual ~SelectTaskScheduler();

	void UpdateChannel(ChannelPtr channel);
	void RemoveChannel(ChannelPtr& channel);
	bool HandleEvent(int timeout);
	
private:
	fd_set fd_read_backup_;		// ���ݶ��¼����ļ����������ϣ�����ÿ��select����ǰ���¹�����
	fd_set fd_write_backup_;	// ����д�¼����ļ����������ϡ�
	fd_set fd_exp_backup_;		// �����쳣�¼����ļ����������ϣ������ӹرգ���
	SOCKET maxfd_ = 0;			// ��ǰ����������ļ�������ֵ������select�ĵ�һ��������

	// ��־λ����ʾ�Ƿ���Ҫ���ö�Ӧ��fd_set������д���쳣����
	bool is_fd_read_reset_ = false;
	bool is_fd_write_reset_ = false;
	bool is_fd_exp_reset_ = false;

	// ����channels_���̰߳�ȫ��
	std::mutex mutex_;
	// �洢Socket��Channel��ӳ�䣬��������ע���Channel��
	std::unordered_map<SOCKET, ChannelPtr> channels_;
};

}

#endif
