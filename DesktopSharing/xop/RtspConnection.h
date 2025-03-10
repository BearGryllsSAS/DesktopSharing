// PHZ
// 2018-6-8

#ifndef _RTSP_CONNECTION_H
#define _RTSP_CONNECTION_H

/*
RtspConnection 是RTSP服务器的核心连接处理器，封装了协议解析、认证、媒体会话管理及RTP传输控制。
其设计通过状态机和协作类实现高效流媒体交互，适用于视频监控、直播等实时场景，具备良好的扩展性和可维护性。
*/

#include "net/EventLoop.h"
#include "net/TcpConnection.h"
#include "RtpConnection.h"
#include "RtspMessage.h"
#include "DigestAuthentication.h"
#include "rtsp.h"
#include <iostream>
#include <functional>
#include <memory>
#include <vector>
#include <cstdint>

namespace xop
{

class RtspServer;
class MediaSession;


/*
RtspConnection 继承自 TcpConnection，负责处理单个RTSP客户端连接的生命周期，包括协议解析、认证、媒体会话管理及RTP/RTCP传输控制。
它是RTSP服务器的核心组件，协调客户端请求与媒体流的交互。
*/
class RtspConnection : public TcpConnection
{
public:
	using CloseCallback = std::function<void (SOCKET sockfd)>;

	enum ConnectionMode
	{
		RTSP_SERVER, 
		RTSP_PUSHER,
		//RTSP_CLIENT,
	};

	enum ConnectionState
	{
		START_CONNECT,
		START_PLAY,
		START_PUSH
	};

	RtspConnection() = delete;
	RtspConnection(std::shared_ptr<Rtsp> rtsp_server, TaskScheduler *task_scheduler, SOCKET sockfd);
	virtual ~RtspConnection();

	MediaSessionId GetMediaSessionId()
	{ return session_id_; }

	TaskScheduler *GetTaskScheduler() const 
	{ return task_scheduler_; }

	void KeepAlive()
	{ alive_count_++; }

	bool IsAlive() const
	{
		if (IsClosed()) {
			return false;
		}

		if(rtp_conn_ != nullptr) {
			if (rtp_conn_->IsMulticast()) {
				return true;
			}			
		}

		return (alive_count_ > 0);
	}

	void ResetAliveCount()
	{ alive_count_ = 0; }

	int GetId() const
	{ return task_scheduler_->GetId(); }

	bool IsPlay() const
	{ return conn_state_ == START_PLAY; }

	bool IsRecord() const
	{ return conn_state_ == START_PUSH; }

private:
	friend class RtpConnection;
	friend class MediaSession;
	friend class RtspServer;
	friend class RtspPusher;

	bool OnRead(BufferReader& buffer);		// 处理接收到的TCP数据。
	void OnClose();							// 连接关闭时释放资源
	void HandleRtcp(SOCKET sockfd);			// 解析RTCP包（如Sender Report、Receiver Report），调整传输策略（如动态码率）。
	void HandleRtcp(BufferReader& buffer);  
	bool HandleRtspRequest(BufferReader& buffer);
	bool HandleRtspResponse(BufferReader& buffer);

	void SendRtspMessage(std::shared_ptr<char> buf, uint32_t size);

	void HandleCmdOption();			// 响应OPTIONS请求，返回服务器支持的方法（如PLAY, TEARDOWN）。
	void HandleCmdDescribe();		// 响应DESCRIBE请求，返回媒体流的SDP描述。
	void HandleCmdSetup();			// 处理SETUP请求，初始化RTP传输参数。
	void HandleCmdPlay();			// 启动媒体流传输，设置conn_state_为START_PLAY。
	void HandleCmdTeardown();		// 
	void HandleCmdGetParamter();	// 
	bool HandleAuthentication();	// 验证客户端请求的Authorization头。

	void SendOptions(ConnectionMode mode= RTSP_SERVER);
	void SendDescribe();
	void SendAnnounce();
	void SendSetup();
	void HandleRecord();

	// 保活计数器，通过KeepAlive()递增，ResetAliveCount()重置，用于检测连接活跃性。
	std::atomic_int alive_count_;
	// 弱引用关联的RtspServer，避免循环依赖，用于访问全局配置和媒体会话。
	std::weak_ptr<Rtsp> rtsp_;
	// 任务调度器指针，管理I/O事件循环（如数据读取、定时任务）。
	xop::TaskScheduler *task_scheduler_ = nullptr;

	// 连接模式：RTSP_SERVER（服务端）、RTSP_PUSHER（推流端）。
	ConnectionMode  conn_mode_ = RTSP_SERVER;
	// 连接状态：START_CONNECT（初始）、START_PLAY（播放中）、START_PUSH（推流中）。
	ConnectionState conn_state_ = START_CONNECT;
	// 关联的媒体会话ID，标识当前连接绑定的媒体流（如摄像头或直播流）。
	MediaSessionId  session_id_ = 0;

	bool has_auth_ = true;
	std::string _nonce;
	// 摘要认证信息，处理RTSP请求的Authorization头验证。
	std::unique_ptr<DigestAuthentication> auth_info_;

	std::shared_ptr<Channel>       rtp_channel_;	// RTP数据传输通道（如UDP或TCP Socket）。
	std::shared_ptr<Channel>       rtcp_channels_[MAX_MEDIA_CHANNEL];
	std::unique_ptr<RtspRequest>   rtsp_request_;
	std::unique_ptr<RtspResponse>  rtsp_response_;
	std::shared_ptr<RtpConnection> rtp_conn_;		// RTP连接管理对象，负责封装RTP包发送和接收逻辑。
};

}

#endif
