// PHZ
// 2018-6-10

#include "RtspConnection.h"
#include "RtspServer.h"
#include "MediaSession.h"
#include "MediaSource.h"
#include "net/SocketUtil.h"

#define USER_AGENT "-_-"
#define RTSP_DEBUG 0
#define MAX_RTSP_MESSAGE_SIZE 2048

using namespace xop;
using namespace std;

RtspConnection::RtspConnection(std::shared_ptr<Rtsp> rtsp, TaskScheduler *task_scheduler, SOCKET sockfd)
	: TcpConnection(task_scheduler, sockfd)
	, rtsp_(rtsp)
	, task_scheduler_(task_scheduler)
	, rtp_channel_(new Channel(sockfd))	// 用客户端连接socket去初始化rtp_channel_（rtp通道）
	, rtsp_request_(new RtspRequest)
	, rtsp_response_(new RtspResponse)
{
	// 连接socket上有事件会调用OnRead函数
	this->SetReadCallback([this](std::shared_ptr<TcpConnection> conn, xop::BufferReader& buffer) {
		return this->OnRead(buffer);
	});

	this->SetCloseCallback([this](std::shared_ptr<TcpConnection> conn) {
		this->OnClose();
	});

	alive_count_ = 1;

	rtp_channel_->SetReadCallback([this]() 
	{ 
		this->HandleRead(); 
	});
	rtp_channel_->SetWriteCallback([this]() { this->HandleWrite(); });
	rtp_channel_->SetCloseCallback([this]() { this->HandleClose(); });
	rtp_channel_->SetErrorCallback([this]() { this->HandleError(); });

	for(int chn=0; chn<MAX_MEDIA_CHANNEL; chn++) {
		rtcp_channels_[chn] = nullptr;
	}

	has_auth_ = true;
	if (rtsp->has_auth_info_) {
		has_auth_ = false;
		auth_info_.reset(new DigestAuthentication(rtsp->realm_, rtsp->username_, rtsp->password_));
	}	
}

RtspConnection::~RtspConnection()
{

}

bool RtspConnection::OnRead(BufferReader& buffer)
{
	KeepAlive();

	int size = buffer.ReadableBytes();
	if (size <= 0) {
		return false; //close
	}

	if (conn_mode_ == RTSP_SERVER) {
		if (!HandleRtspRequest(buffer)){
			return false; 
		}
	}
	else if (conn_mode_ == RTSP_PUSHER) {
		if (!HandleRtspResponse(buffer)) {           
			return false;
		}
	}

	if (buffer.ReadableBytes() > MAX_RTSP_MESSAGE_SIZE) {
		buffer.RetrieveAll(); 
	}

	return true;
}

void RtspConnection::OnClose()
{
	if(session_id_ != 0) {
		auto rtsp = rtsp_.lock();
		if (rtsp) {
			MediaSession::Ptr media_session = rtsp->LookMediaSession(session_id_);
			if (media_session) {
				media_session->RemoveClient(this->GetSocket());
			}
		}	
	}

	for(int chn=0; chn<MAX_MEDIA_CHANNEL; chn++) {
		if(rtcp_channels_[chn] && !rtcp_channels_[chn]->IsNoneEvent()) {
			task_scheduler_->RemoveChannel(rtcp_channels_[chn]);
		}
	}
}

bool RtspConnection::HandleRtspRequest(BufferReader& buffer)
{
#if RTSP_DEBUG
	string str(buffer.Peek(), buffer.ReadableBytes());
	if (str.find("rtsp") != string::npos || str.find("RTSP") != string::npos)
	{
		std::cout << str << std::endl;
	}
#endif

	if (rtsp_request_->ParseRequest(&buffer)) {
		RtspRequest::Method method = rtsp_request_->GetMethod();
		if(method == RtspRequest::RTCP) {
			HandleRtcp(buffer);
			return true;
		}
		else if(!rtsp_request_->GotAll()) {
			return true;
		}
        
		switch (method)
		{
		case RtspRequest::OPTIONS:
			HandleCmdOption();
			break;
		case RtspRequest::DESCRIBE:
			HandleCmdDescribe();
			break;
		case RtspRequest::SETUP:
			HandleCmdSetup();
			break;
		case RtspRequest::PLAY:
			HandleCmdPlay();
			break;
		case RtspRequest::TEARDOWN:
			HandleCmdTeardown();
			break;
		case RtspRequest::GET_PARAMETER:
			HandleCmdGetParamter();
			break;
		default:
			break;
		}

		if (rtsp_request_->GotAll()) {
			rtsp_request_->Reset();
		}
	}
	else {
		return false;
	}

	return true;
}

bool RtspConnection::HandleRtspResponse(BufferReader& buffer)
{
#if RTSP_DEBUG
	string str(buffer.Peek(), buffer.ReadableBytes());
	if (str.find("rtsp") != string::npos || str.find("RTSP") != string::npos) {
		cout << str << endl;
	}		
#endif

	if (rtsp_response_->ParseResponse(&buffer)) {
		RtspResponse::Method method = rtsp_response_->GetMethod();
		switch (method)
		{
		case RtspResponse::OPTIONS:
			if (conn_mode_ == RTSP_PUSHER) {
				SendAnnounce();
			}             
			break;
		case RtspResponse::ANNOUNCE:
		case RtspResponse::DESCRIBE:
			SendSetup();
			break;
		case RtspResponse::SETUP:
			SendSetup();
			break;
		case RtspResponse::RECORD:
			HandleRecord();
			break;
		default:            
			break;
		}
	}
	else {
		return false;
	}

	return true;
}

void RtspConnection::SendRtspMessage(std::shared_ptr<char> buf, uint32_t size)
{
#if RTSP_DEBUG
	cout << buf.get() << endl;
#endif

	this->Send(buf, size);
	return;
}

void RtspConnection::HandleRtcp(BufferReader& buffer)
{    
	char *peek = buffer.Peek();
	if(peek[0] == '$' &&  buffer.ReadableBytes() > 4) {
		uint32_t pkt_size = peek[2]<<8 | peek[3];
		if(pkt_size +4 >=  buffer.ReadableBytes()) {
			buffer.Retrieve(pkt_size +4);  
		}
	}
}
 
void RtspConnection::HandleRtcp(SOCKET sockfd)
{
	char buf[1024] = {0};
	if(recv(sockfd, buf, 1024, 0) > 0) {
		KeepAlive();
	}
}

// 处理 RTSP 协议中的 OPTIONS 命令的响应逻辑
void RtspConnection::HandleCmdOption()
{
	std::shared_ptr<char> res(new char[2048], std::default_delete<char[]>());
	int size = rtsp_request_->BuildOptionRes(res.get(), 2048);
	this->SendRtspMessage(res, size);	
}

/*
这段代码是 RtspConnection 类中处理 RTSP DESCRIBE 请求的方法 HandleCmdDescribe，主要功能是生成媒体资源的 SDP 描述并返回给客户端。
*/
void RtspConnection::HandleCmdDescribe()
{
	/*
	目的：检查客户端是否通过认证。
		auth_info_ 不为空表示需要认证。
		HandleAuthentication() 返回 false 表示认证失败，直接返回，终止处理。
	*/
	if (auth_info_!=nullptr && !HandleAuthentication()) {
		return;
	}

	/*
	​目的：为当前连接创建 RTP 传输通道。
		rtp_conn_ 是 RtpConnection 的智能指针，管理 RTP/RTCP 数据传输。
		shared_from_this() 将当前 RtspConnection 对象的所有权共享给 RtpConnection，确保生命周期安全。
	*/
	if (rtp_conn_ == nullptr) {
		rtp_conn_.reset(new RtpConnection(shared_from_this()));
	}

	/*
	准备响应缓冲区

	​目的：分配 4096 字节的缓冲区 res，用于构建 RTSP 响应。
		使用 std::shared_ptr 管理内存，确保自动释放。
	*/
	int size = 0;
	std::shared_ptr<char> res(new char[4096], std::default_delete<char[]>());
	

	/*
	查找媒体会话

	目的：根据客户端请求的 URL 后缀查找对应的媒体会话。
		rtsp_ 是 RtspServer 的弱指针，通过 lock() 升级为共享指针。
		LookMediaSession 通过 URL 后缀（如 /live）查找已注册的 MediaSession。
	*/
	MediaSession::Ptr media_session = nullptr;
	auto rtsp = rtsp_.lock();
	if (rtsp) {
		media_session = rtsp->LookMediaSession(rtsp_request_->GetRtspUrlSuffix());
	}
	
	
	if(!rtsp || !media_session) {
			/*
		处理媒体会话不存在的情况

		目的：如果服务器未找到媒体会话，构建 404 Not Found 响应。
			BuildNotFoundRes 生成错误响应内容，写入 res 缓冲区。
		*/
		size = rtsp_request_->BuildNotFoundRes(res.get(), 4096);
	}
	else {
		session_id_ = media_session->GetMediaSessionId();
		media_session->AddClient(this->GetSocket(), rtp_conn_);

		// 配置 RTP 通道参数
		/*
		配置会话和 RTP 参数：
			记录会话 ID session_id_。
			将当前客户端 Socket 和 RTP 连接添加到媒体会话中（AddClient），用于后续数据传输。
			遍历所有媒体通道（如音视频），设置 RTP 时钟频率和负载类型。
		*/
		/*
		1 与 SDP 描述的关联
​			(1)SDP 描述：在 media_session->GetSdpMessage() 生成的 SDP 中，会包含如下信息：
				a=rtpmap:96 H264/90000     // 负载类型 96，编码 H264，时钟频率 90000 Hz
				a=rtpmap:97 AAC/44100      // 负载类型 97，编码 AAC，时钟频率 44100 Hz
​			
			(2)一致性要求：RTP 连接中设置的参数必须与 SDP 描述完全一致，否则接收端无法正确解析数据。
		​
		2 接收端的处理流程
			接收端通过 SDP 得知负载类型和时钟频率。
			接收端根据负载类型初始化解码器。
			接收端根据时钟频率计算时间戳，实现同步播放。
		​
		3 总结
​			时钟频率：告诉接收端如何将时间戳转换为真实时间，解决“何时播放”的问题。
​			负载类型：告诉接收端如何解码数据，解决“如何播放”的问题。
​			代码意义：在 RTSP 的 DESCRIBE 阶段，这些参数通过 SDP 发送给客户端，为后续的 SETUP 和 PLAY 请求奠定基础。
		*/
		for(int chn=0; chn<MAX_MEDIA_CHANNEL; chn++) {
			MediaSource* source = media_session->GetMediaSource((MediaChannelId)chn);
			if(source != nullptr) {
				rtp_conn_->SetClockRate((MediaChannelId)chn, source->GetClockRate());
				rtp_conn_->SetPayloadType((MediaChannelId)chn, source->GetPayloadType());
			}
		}

		// 生成 SDP 描述
		/*
		生成 SDP：
			GetSdpMessage 生成 SDP 描述，包含媒体格式、IP 地址、端口等信息。
			如果生成失败，返回 500 Server Error；否则返回 200 OK 和 SDP。
		*/
		std::string sdp = media_session->GetSdpMessage(SocketUtil::GetSocketIp(this->GetSocket()), rtsp->GetVersion());
		if(sdp == "") {
			size = rtsp_request_->BuildServerErrorRes(res.get(), 4096);
		}
		else {
			size = rtsp_request_->BuildDescribeRes(res.get(), 4096, sdp.c_str());		
		}
	}

	// 通过 SendRtspMessage 将构建好的响应发送给客户端。
	SendRtspMessage(res, size);
	return ;
}

/*
这段代码是RTSP连接处理SETUP命令的方法，主要功能是根据客户端的SETUP请求配置媒体传输参数，并返回相应的响应。

关键点总结
​	身份验证：优先验证客户端权限。
​	会话有效性：确保媒体会话和服务器实例存在。
​	传输模式分支：
​		多播：配置组播地址和端口。
​		TCP：通过指定通道传输。
​		UDP：监听RTCP报文，处理网络统计。
​	错误处理：集中处理错误，返回标准RTSP错误码。
​	资源管理：使用shared_ptr管理缓冲区，避免内存泄漏。
*/
void RtspConnection::HandleCmdSetup()
{
	// 这个函数会进入两次

	// 第一次：协商 h264
	/*
	SETUP rtsp://127.0.0.1:8554/live/track0 RTSP/1.0
	CSeq: 4
	User-Agent: LibVLC/3.0.21 (LIVE555 Streaming Media v2016.11.28)
	Transport: RTP/AVP;unicast;client_port=50296-50297
	*/

	// 第二次：协商 aac
	/*
	SETUP rtsp://127.0.0.1:8554/live/track1 RTSP/1.0
	CSeq: 5
	User-Agent: LibVLC/3.0.21 (LIVE555 Streaming Media v2016.11.28)
	Transport: RTP/AVP;unicast;client_port=65492-65493
	Session: 44984

	*/

	/*
	正常行为：
		两次 SETUP 请求是 RTSP 协议中处理多轨道的标准流程。
​	关键点：
		每个轨道独立配置传输参数。
		复用同一会话 ID 管理多个轨道。
​		调试方向：检查 SDP 声明、会话 ID 传递、端口分配是否合理。
	*/

	/*
	身份验证

	作用：
		检查是否需要身份验证。如果auth_info_存在且验证失败（HandleAuthentication()返回false），则直接返回，不再处理后续逻辑。
​	解析：
		确保只有通过验证的客户端才能继续配置媒体传输。
	*/
	if (auth_info_ != nullptr && !HandleAuthentication()) {
		return;
	}

	/*
	初始化变量和资源

	作用：
		分配4096字节的缓冲区res，用于构建响应消息。
		获取当前请求的媒体通道ID（channel_id，如音频或视频）。
		通过弱指针rtsp_.lock()获取RTSP服务器的共享指针rtsp，并查找对应的媒体会话media_session。
​	解析：
		媒体会话（media_session）是管理媒体流的核心对象，必须存在才能继续处理。
	*/
	int size = 0;
	std::shared_ptr<char> res(new char[4096], std::default_delete<char[]>());
	MediaChannelId channel_id = rtsp_request_->GetChannelId();
	MediaSession::Ptr media_session = nullptr;

	auto rtsp = rtsp_.lock();
	if (rtsp) {
		media_session = rtsp->LookMediaSession(session_id_);
	}

	/*
	错误处理：会话不存在
	
	作用：
		如果RTSP服务器实例或媒体会话不存在，跳转到server_error标签，返回服务器错误。
	​解析：
		可能是会话ID无效或服务器已关闭。
	*/
	if(!rtsp || !media_session)  {
		goto server_error;
	}

	if(media_session->IsMulticast())  {	// 多播模式处理
		std::string multicast_ip = media_session->GetMulticastIp();
		if(rtsp_request_->GetTransportMode() == RTP_OVER_MULTICAST) {
			uint16_t port = media_session->GetMulticastPort(channel_id);
			uint16_t session_id = rtp_conn_->GetRtpSessionId();
			if (!rtp_conn_->SetupRtpOverMulticast(channel_id, multicast_ip.c_str(), port)) {
				goto server_error;
			}

			size = rtsp_request_->BuildSetupMulticastRes(res.get(), 4096, multicast_ip.c_str(), port, session_id);
		}
		else {
			goto transport_unsupport;
		}
	}
	else { // 单播模式处理
		if(rtsp_request_->GetTransportMode() == RTP_OVER_TCP) {
			uint16_t rtp_channel = rtsp_request_->GetRtpChannel();
			uint16_t rtcp_channel = rtsp_request_->GetRtcpChannel();
			uint16_t session_id = rtp_conn_->GetRtpSessionId();

			rtp_conn_->SetupRtpOverTcp(channel_id, rtp_channel, rtcp_channel);
			size = rtsp_request_->BuildSetupTcpRes(res.get(), 4096, rtp_channel, rtcp_channel, session_id);
		}
		else if(rtsp_request_->GetTransportMode() == RTP_OVER_UDP) {
			uint16_t peer_rtp_port = rtsp_request_->GetRtpPort();
			uint16_t peer_rtcp_port = rtsp_request_->GetRtcpPort();
			uint16_t session_id = rtp_conn_->GetRtpSessionId();

			// 通过 UDP 协议建立 RTP/RTCP 传输通道，核心步骤包括绑定本地端口、配置对端地址及优化缓冲区
			if(rtp_conn_->SetupRtpOverUdp(channel_id, peer_rtp_port, peer_rtcp_port)) {
				SOCKET rtcp_fd = rtp_conn_->GetRtcpSocket(channel_id);
				rtcp_channels_[channel_id].reset(new Channel(rtcp_fd));
				// HandleRtcp 仅仅保活用，不做任何控制
				rtcp_channels_[channel_id]->SetReadCallback([rtcp_fd, this]() { this->HandleRtcp(rtcp_fd); });
				rtcp_channels_[channel_id]->EnableReading();
				task_scheduler_->UpdateChannel(rtcp_channels_[channel_id]);
			}
			else {
				goto server_error;
			}

			uint16_t serRtpPort = rtp_conn_->GetRtpPort(channel_id);
			uint16_t serRtcpPort = rtp_conn_->GetRtcpPort(channel_id);
			size = rtsp_request_->BuildSetupUdpRes(res.get(), 4096, serRtpPort, serRtcpPort, session_id);
		}
		else {          
			goto transport_unsupport;
		}
	}

	// 发送响应
	SendRtspMessage(res, size);
	return ;

transport_unsupport:
	size = rtsp_request_->BuildUnsupportedRes(res.get(), 4096);
	SendRtspMessage(res, size);
	return ;

server_error:
	size = rtsp_request_->BuildServerErrorRes(res.get(), 4096);
	SendRtspMessage(res, size);
	return ;
}

void RtspConnection::HandleCmdPlay()
{
	if (auth_info_ != nullptr) {
		if (!HandleAuthentication()) {
			return;
		}
	}

	if (rtp_conn_ == nullptr) {
		return;
	}

	conn_state_ = START_PLAY;
	rtp_conn_->Play();

	uint16_t session_id = rtp_conn_->GetRtpSessionId();
	std::shared_ptr<char> res(new char[2048], std::default_delete<char[]>());

	int size = rtsp_request_->BuildPlayRes(res.get(), 2048, nullptr, session_id);
	SendRtspMessage(res, size);
}

void RtspConnection::HandleCmdTeardown()
{
	if (rtp_conn_ == nullptr) {
		return;
	}

	rtp_conn_->Teardown();

	uint16_t session_id = rtp_conn_->GetRtpSessionId();
	std::shared_ptr<char> res(new char[2048], std::default_delete<char[]>());
	int size = rtsp_request_->BuildTeardownRes(res.get(), 2048, session_id);
	SendRtspMessage(res, size);

	//HandleClose();
}

/*
代码功能：响应 RTSP GET_PARAMETER 请求，用于会话保活或简单参数确认。
​调用时机：客户端在会话建立后发送 GET_PARAMETER 请求（如保活或查询参数）。
​现状局限：仅实现保活逻辑，未支持具体参数查询。
​协议合规性：符合基础要求，但功能不完整，需扩展参数处理逻辑。
*/
void RtspConnection::HandleCmdGetParamter()
{
	if (rtp_conn_ == nullptr) {
		return;
	}

	uint16_t session_id = rtp_conn_->GetRtpSessionId();
	std::shared_ptr<char> res(new char[2048], std::default_delete<char[]>());
	int size = rtsp_request_->BuildGetParamterRes(res.get(), 2048, session_id);
	SendRtspMessage(res, size);
}

bool RtspConnection::HandleAuthentication()
{
	if (auth_info_ != nullptr && !has_auth_) {
		std::string cmd = rtsp_request_->MethodToString[rtsp_request_->GetMethod()];
		std::string url = rtsp_request_->GetRtspUrl();

		if (_nonce.size() > 0 && (auth_info_->GetResponse(_nonce, cmd, url) == rtsp_request_->GetAuthResponse())) {
			_nonce.clear();
			has_auth_ = true;
		}
		else {
			std::shared_ptr<char> res(new char[4096], std::default_delete<char[]>());
			_nonce = auth_info_->GetNonce();
			int size = rtsp_request_->BuildUnauthorizedRes(res.get(), 4096, auth_info_->GetRealm().c_str(), _nonce.c_str());
			SendRtspMessage(res, size);
			return false;
		}
	}

	return true;
}

void RtspConnection::SendOptions(ConnectionMode mode)
{
	if (rtp_conn_ == nullptr) {
		rtp_conn_.reset(new RtpConnection(shared_from_this()));
	}

	auto rtsp = rtsp_.lock();
	if (!rtsp) {
		HandleClose();
		return;
	}	

	conn_mode_ = mode;
	rtsp_response_->SetUserAgent(USER_AGENT);
	rtsp_response_->SetRtspUrl(rtsp->GetRtspUrl().c_str());

	std::shared_ptr<char> req(new char[2048], std::default_delete<char[]>());
	int size = rtsp_response_->BuildOptionReq(req.get(), 2048);
	SendRtspMessage(req, size);
}

void RtspConnection::SendAnnounce()
{
	MediaSession::Ptr media_session = nullptr;

	auto rtsp = rtsp_.lock();
	if (rtsp) {
		media_session = rtsp->LookMediaSession(1);
	}

	if (!rtsp || !media_session) {
		HandleClose();
		return;
	}
	else {
		session_id_ = media_session->GetMediaSessionId();
		media_session->AddClient(this->GetSocket(), rtp_conn_);

		for (int chn = 0; chn<2; chn++) {
			MediaSource* source = media_session->GetMediaSource((MediaChannelId)chn);
			if (source != nullptr) {
				rtp_conn_->SetClockRate((MediaChannelId)chn, source->GetClockRate());
				rtp_conn_->SetPayloadType((MediaChannelId)chn, source->GetPayloadType());
			}
		}
	}

	std::string sdp = media_session->GetSdpMessage(SocketUtil::GetSocketIp(this->GetSocket()), rtsp->GetVersion());
	if (sdp == "") {
		HandleClose();
		return;
	}

	std::shared_ptr<char> req(new char[4096], std::default_delete<char[]>());
	int size = rtsp_response_->BuildAnnounceReq(req.get(), 4096, sdp.c_str());
	SendRtspMessage(req, size);
}

void RtspConnection::SendDescribe()
{
	std::shared_ptr<char> req(new char[2048], std::default_delete<char[]>());
	int size = rtsp_response_->BuildDescribeReq(req.get(), 2048);
	SendRtspMessage(req, size);
}

void RtspConnection::SendSetup()
{
	int size = 0;
	std::shared_ptr<char> buf(new char[2048], std::default_delete<char[]>());
	MediaSession::Ptr media_session = nullptr;

	auto rtsp = rtsp_.lock();
	if (rtsp) {
		media_session = rtsp->LookMediaSession(session_id_);
	}
	
	if (!rtsp || !media_session) {
		HandleClose();
		return;
	}

	if (media_session->GetMediaSource(channel_0) && !rtp_conn_->IsSetup(channel_0)) {
		rtp_conn_->SetupRtpOverTcp(channel_0, 0, 1);
		size = rtsp_response_->BuildSetupTcpReq(buf.get(), 2048, channel_0);
	}
	else if (media_session->GetMediaSource(channel_1) && !rtp_conn_->IsSetup(channel_1)) {
		rtp_conn_->SetupRtpOverTcp(channel_1, 2, 3);
		size = rtsp_response_->BuildSetupTcpReq(buf.get(), 2048, channel_1);
	}
	else {
		size = rtsp_response_->BuildRecordReq(buf.get(), 2048);
	}

	SendRtspMessage(buf, size);
}

void RtspConnection::HandleRecord()
{
	conn_state_ = START_PUSH;
	rtp_conn_->Record();
}
