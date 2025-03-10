#include "ScreenLive.h"
#include "net/NetInterface.h"
#include "net/Timestamp.h"
#include "xop/RtspServer.h"
#include "xop/H264Parser.h"
#include "ScreenCapture/DXGIScreenCapture.h"
#include "ScreenCapture/GDIScreenCapture.h"
#include <versionhelpers.h>

ScreenLive::ScreenLive()
	: event_loop_(new xop::EventLoop)
{
	encoding_fps_ = 0;
	rtsp_clients_.clear();
}

ScreenLive::~ScreenLive()
{
	Destroy();
}

ScreenLive& ScreenLive::Instance()
{
	static ScreenLive s_screen_live;
	return s_screen_live;
}

bool ScreenLive::GetScreenImage(std::vector<uint8_t>& bgra_image, uint32_t& width, uint32_t& height)
{
	if (screen_capture_) {
		if (screen_capture_->CaptureFrame(bgra_image, width, height)) {
			return true;
		}
	}

	return false;
}

std::string ScreenLive::GetStatusInfo()
{
	std::string info;

	if (is_encoder_started_) {
		info += "Encoder: " + av_config_.codec + " \n\n";
		info += "Encoding framerate: " + std::to_string(encoding_fps_) + " \n\n";
	}

	if (rtsp_server_ != nullptr) {
		info += "RTSP Server (connections): " + std::to_string(rtsp_clients_.size()) + " \n\n";
	}

	if (rtsp_pusher_ != nullptr) {		
		std::string status = rtsp_pusher_->IsConnected() ? "connected" : "disconnected";
		info += "RTSP Pusher: " + status + " \n\n";
	}

	if (rtmp_pusher_ != nullptr) {
		std::string status = rtmp_pusher_->IsConnected() ? "connected" : "disconnected";
		info += "RTMP Pusher: " + status + " \n\n";
	}

	return info;
}

bool ScreenLive::Init(AVConfig& config)
{
	if (is_initialized_) {
		Destroy();
	}
	
	if (StartCapture() < 0) {
		return false;
	}

	if (StartEncoder(config) < 0) {
		return false;
	}

	is_initialized_ = true;
	return true;
}

void ScreenLive::Destroy()
{
	{
		std::lock_guard<std::mutex> locker(mutex_);

		if (rtsp_pusher_ != nullptr && rtsp_pusher_->IsConnected()) {
			rtsp_pusher_->Close();
			rtsp_pusher_ = nullptr;
		}

		if (rtmp_pusher_ != nullptr && rtmp_pusher_->IsConnected()) {
			rtmp_pusher_->Close();
			rtmp_pusher_ = nullptr;
		}

		if (rtsp_server_ != nullptr) {
			rtsp_server_->RemoveSession(media_session_id_);
			rtsp_server_ = nullptr;
		}
	}

	StopEncoder();
	StopCapture();
	is_initialized_ = false;
}

bool ScreenLive::StartLive(int type, LiveConfig& config)
{
	if (!is_encoder_started_) {
		return false;
	}

	// 采样率
	uint32_t samplerate = audio_capture_.GetSamplerate();
	// 声道数
	uint32_t channels = audio_capture_.GetChannels();

	if (type == SCREEN_LIVE_RTSP_SERVER) {	
		// event_loop_ 在 main 函数中第一次创建 ScreenLive 示例的时候已经创建了，全局只有一个
		auto rtsp_server = xop::RtspServer::Create(event_loop_.get());
		xop::MediaSessionId session_id = 0;

		if (config.ip == "127.0.0.1") {
			config.ip = "0.0.0.0";
		}

		if (!rtsp_server->Start(config.ip, config.port)) {
			return false;
		}

		// 初始化 MediaSession，以及其中一个重要成员变量 std::vector<std::unique_ptr<MediaSource>> media_sources_ 的初始化
		xop::MediaSession* session = xop::MediaSession::CreateNew(config.suffix);
		session->AddSource(xop::channel_0, xop::H264Source::CreateNew());
		session->AddSource(xop::channel_1, xop::AACSource::CreateNew(samplerate, channels, false));
		session->AddNotifyConnectedCallback([this](xop::MediaSessionId sessionId, std::string peer_ip, uint16_t peer_port) {			
			this->rtsp_clients_.emplace(peer_ip + ":" + std::to_string(peer_port));
			printf("RTSP client: %u\n", this->rtsp_clients_.size());
		});
		session->AddNotifyDisconnectedCallback([this](xop::MediaSessionId sessionId, std::string peer_ip, uint16_t peer_port) {			
			this->rtsp_clients_.erase(peer_ip + ":" + std::to_string(peer_port));
			printf("RTSP client: %u\n", this->rtsp_clients_.size());
		});

		// 配置好 session 后，保存映射到 session_id 中
		session_id = rtsp_server->AddSession(session); // ！！！配置好 session 后，把其与 rtsp_server 进行关联！！！
		//printf("RTSP Server: rtsp://%s:%hu/%s \n", xop::NetInterface::GetLocalIPAddress().c_str(), config.port, config.suffix.c_str());
		printf("RTSP Server start: rtsp://%s:%hu/%s \n", config.ip.c_str(), config.port, config.suffix.c_str());

		std::lock_guard<std::mutex> locker(mutex_);
		rtsp_server_ = rtsp_server;
		// 最后将 session_id 保存到成员变量 media_session_id_ 中
		media_session_id_ = session_id;
	}
	else if (type == SCREEN_LIVE_RTSP_PUSHER) {
		auto rtsp_pusher = xop::RtspPusher::Create(event_loop_.get());
		xop::MediaSession *session = xop::MediaSession::CreateNew();
		session->AddSource(xop::channel_0, xop::H264Source::CreateNew());
		session->AddSource(xop::channel_1, xop::AACSource::CreateNew(audio_capture_.GetSamplerate(), audio_capture_.GetChannels(), false));
		
		rtsp_pusher->AddSession(session);
		if (rtsp_pusher->OpenUrl(config.rtsp_url, 1000) != 0) {
			rtsp_pusher = nullptr;
			printf("RTSP Pusher: Open url(%s) failed. \n", config.rtsp_url.c_str());
			return false;
		}

		std::lock_guard<std::mutex> locker(mutex_);
		rtsp_pusher_ = rtsp_pusher;
		printf("RTSP Pusher start: Push stream to  %s ... \n", config.rtsp_url.c_str());
	}
	else if (type == SCREEN_LIVE_RTMP_PUSHER) {
		auto rtmp_pusher = xop::RtmpPublisher::Create(event_loop_.get());

		xop::MediaInfo mediaInfo;
		uint8_t extradata[1024] = { 0 };
		int  extradata_size = 0;

		extradata_size = aac_encoder_.GetSpecificConfig(extradata, 1024);
		if (extradata_size <= 0) {
			printf("Get audio specific config failed. \n");
			return false;
		}

		mediaInfo.audio_specific_config_size = extradata_size;
		mediaInfo.audio_specific_config.reset(new uint8_t[mediaInfo.audio_specific_config_size], std::default_delete<uint8_t[]>());
		memcpy(mediaInfo.audio_specific_config.get(), extradata, extradata_size);

		extradata_size = h264_encoder_.GetSequenceParams(extradata, 1024);
		if (extradata_size <= 0) {
			printf("Get video specific config failed. \n");
			return false;
		}

		xop::Nal sps = xop::H264Parser::findNal((uint8_t*)extradata, extradata_size);
		if (sps.first != nullptr && sps.second != nullptr && ((*sps.first & 0x1f) == 7)) {
			mediaInfo.sps_size = sps.second - sps.first + 1;
			mediaInfo.sps.reset(new uint8_t[mediaInfo.sps_size], std::default_delete<uint8_t[]>());
			memcpy(mediaInfo.sps.get(), sps.first, mediaInfo.sps_size);

			xop::Nal pps = xop::H264Parser::findNal(sps.second, extradata_size - (sps.second - (uint8_t*)extradata));
			if (pps.first != nullptr && pps.second != nullptr && ((*pps.first&0x1f) == 8)) {
				mediaInfo.pps_size = pps.second - pps.first + 1;
				mediaInfo.pps.reset(new uint8_t[mediaInfo.pps_size], std::default_delete<uint8_t[]>());
				memcpy(mediaInfo.pps.get(), pps.first, mediaInfo.pps_size);
			}
		}

		rtmp_pusher->SetMediaInfo(mediaInfo);

		std::string status;
		if (rtmp_pusher->OpenUrl(config.rtmp_url, 1000, status) < 0) {
			printf("RTMP Pusher: Open url(%s) failed. \n", config.rtmp_url.c_str());
			return false;
		}

		std::lock_guard<std::mutex> locker(mutex_);
		rtmp_pusher_ = rtmp_pusher;
		printf("RTMP Pusher start: Push stream to  %s ... \n", config.rtmp_url.c_str());
	}
	else {
		return false;
	}

	return true;
}

void ScreenLive::StopLive(int type)
{
	std::lock_guard<std::mutex> locker(mutex_);

	switch (type)
	{
	case SCREEN_LIVE_RTSP_SERVER:
		if (rtsp_server_ != nullptr) {
			rtsp_server_->Stop();
			rtsp_server_ = nullptr;
			rtsp_clients_.clear();
			printf("RTSP Server stop. \n");
		}
		
		break;

	case SCREEN_LIVE_RTSP_PUSHER:
		if (rtsp_pusher_ != nullptr) {
			rtsp_pusher_->Close();
			rtsp_pusher_ = nullptr;
			printf("RTSP Pusher stop. \n");
		}		
		break;

	case SCREEN_LIVE_RTMP_PUSHER:
		if (rtmp_pusher_ != nullptr) {
			rtmp_pusher_->Close();
			rtmp_pusher_ = nullptr;
			printf("RTMP Pusher stop. \n");
		}
		break;

	default:
		break;
	}
}

bool ScreenLive::IsConnected(int type)
{
	std::lock_guard<std::mutex> locker(mutex_);

	bool is_connected = false;
	switch (type)
	{
	case SCREEN_LIVE_RTSP_SERVER:
		if (rtsp_server_ != nullptr) {
			is_connected = rtsp_clients_.size() > 0;
		}		
		break;

	case SCREEN_LIVE_RTSP_PUSHER:
		if (rtsp_pusher_ != nullptr) {
			is_connected = rtsp_pusher_->IsConnected();
		}		
		break;

	case SCREEN_LIVE_RTMP_PUSHER:
		if (rtmp_pusher_ != nullptr) {
			is_connected = rtmp_pusher_->IsConnected();
		}
		break;

	default:
		break;
	}

	return is_connected;
}

/*
ScreenLive::StartCapture() 函数的目的是初始化并启动屏幕和音频捕获功能。
它支持通过 DXGI（DirectX）或 GDI（图形设备接口）来捕获屏幕内容，同时也初始化音频捕获功能。
*/
int ScreenLive::StartCapture()
{
	// 获取所有连接的显示器信息
	std::vector<DX::Monitor> monitors = DX::GetMonitors();
	if (monitors.empty()) {
		printf("Monitor not found. \n");
		return -1;
	}

	// 打印所有连接的显示器的分辨率
	for (size_t index = 0; index < monitors.size(); index++) {
		printf("Monitor(%u) info: %dx%d \n", index,
			monitors[index].right - monitors[index].left, 
			monitors[index].bottom - monitors[index].top);
	}
	
	// 默认选择索引为0的显示器
	int display_index = 0; // monitor index

	if (!screen_capture_) {
		// 操作系统版本是否是 Windows8 或更高版本
		if (IsWindows8OrGreater()) {
			// 尝试 DXGI 进行屏幕捕获，如果失败，则再换到 GDI
			printf("DXGI Screen capture start, monitor index: %d \n", display_index);
			screen_capture_ = new DXGIScreenCapture();
			if (!screen_capture_->Init(display_index)) {
				printf("DXGI Screen capture start failed, monitor index: %d \n", display_index);
				delete screen_capture_;

				printf("GDI Screen capture start, monitor index: %d \n", display_index);
				screen_capture_ = new GDIScreenCapture();
			}
		}
		else {
			printf("GDI Screen capture start, monitor index: %d \n", display_index);
			screen_capture_ = new GDIScreenCapture();
		}

		// 屏幕捕获失败
		if (!screen_capture_->Init(display_index)) {
			printf("Screen capture start failed, monitor index: %d \n", display_index);
			delete screen_capture_;
			screen_capture_ = nullptr;
			return -1;
		}
	}
	
	// 音频捕获
	if (!audio_capture_.Init()) {
		return -1;
	}

	is_capture_started_ = true;
	return 0;
}

int ScreenLive::StopCapture()
{
	if (is_capture_started_) {
		if (screen_capture_) {
			screen_capture_->Destroy();
			delete screen_capture_;
			screen_capture_ = nullptr;
		}
		audio_capture_.Destroy();
		is_capture_started_ = false;
	}

	return 0;
}

int ScreenLive::StartEncoder(AVConfig& config)
{
	if (!is_capture_started_) {
		return -1;
	}

	av_config_ = config;

	ffmpeg::AVConfig encoder_config;
	encoder_config.video.framerate = av_config_.framerate;
	encoder_config.video.bitrate = av_config_.bitrate_bps;
	encoder_config.video.gop = av_config_.framerate;
	encoder_config.video.format = AV_PIX_FMT_BGRA;
	encoder_config.video.width = screen_capture_->GetWidth();
	encoder_config.video.height = screen_capture_->GetHeight();

	h264_encoder_.SetCodec(config.codec);

	if (!h264_encoder_.Init(av_config_.framerate, av_config_.bitrate_bps/1000,
							AV_PIX_FMT_BGRA, screen_capture_->GetWidth(), 
							screen_capture_->GetHeight()) ) {
		return -1;
	}

	int samplerate = audio_capture_.GetSamplerate();
	int channels = audio_capture_.GetChannels();
	if (!aac_encoder_.Init(samplerate, channels, AV_SAMPLE_FMT_S16, 64)) {
		return -1;
	}

	// 开启视频和音频编码线程
	is_encoder_started_ = true;
	encode_video_thread_.reset(new std::thread(&ScreenLive::EncodeVideo, this));
	encode_audio_thread_.reset(new std::thread(&ScreenLive::EncodeAudio, this));
	return 0;
}

int ScreenLive::StopEncoder() 
{
	if (is_encoder_started_) {
		is_encoder_started_ = false;

		if (encode_video_thread_) {
			encode_video_thread_->join();
			encode_video_thread_ = nullptr;
		}

		if (encode_audio_thread_) {
			encode_audio_thread_->join();
			encode_audio_thread_ = nullptr;
		}

		h264_encoder_.Destroy();
		aac_encoder_.Destroy();
	}

	return 0;
}

bool ScreenLive::IsKeyFrame(const uint8_t* data, uint32_t size)
{
	if (size > 4) {
		//0x67:sps ,0x65:IDR, 0x6: SEI
		if (data[4] == 0x67 || data[4] == 0x65 || 
			data[4] == 0x6 || data[4] == 0x27) {
			return true;
		}
	}

	return false;
}

void ScreenLive::EncodeVideo()
{
	static xop::Timestamp encoding_ts, update_ts;	// 静态时间戳对象，用于帧率控制
	uint32_t encoding_fps = 0;						// 临时帧率计数器
	uint32_t msec = 1000 / av_config_.framerate;	// 每帧理论间隔（ms）


	while (is_encoder_started_ && is_capture_started_) {
		// 帧率统计与更新
		if (update_ts.Elapsed() >= 1000) {
			update_ts.Reset();
			encoding_fps_ = encoding_fps; // 更新成员变量
			encoding_fps = 0;             // 重置计数器
		}

		// 计算本帧需等待的时间
		uint32_t delay = msec;
		uint32_t elapsed = (uint32_t)encoding_ts.Elapsed();
		if (elapsed > delay) {
			delay = 0;          // 超时，立即处理下一帧
		}
		else {
			delay -= elapsed;   // 剩余等待时间
		}

		// 等待至下一帧时间点
		std::this_thread::sleep_for(std::chrono::milliseconds(delay));
		encoding_ts.Reset();    // 重置编码时间戳

		// 捕获屏幕帧
		std::vector<uint8_t> bgra_image;
		uint32_t timestamp = xop::H264Source::GetTimestamp();
		int frame_size = 0;
		uint32_t width = 0, height = 0;

		if (screen_capture_->CaptureFrame(bgra_image, width, height)) {
			// H.264编码
			std::vector<uint8_t> out_frame;
			if (h264_encoder_.Encode(&bgra_image[0], width, height, bgra_image.size(), out_frame) > 0) {
				if (out_frame.size() > 0) {
					encoding_fps += 1;	// 帧率计数
					PushVideo(&out_frame[0], out_frame.size(), timestamp);	// 推送数据
				}
			}
		}
	}

	encoding_fps_ = 0;
}

/*
这段代码是 ScreenLive 类的音频编码线程函数，负责从音频采集设备读取PCM数据，编码为AAC格式，并推送编码后的数据。

功能：
	实时音频采集 → PCM数据读取 → AAC编码 → 数据推送。
优点：
	通过休眠避免忙等待，代码结构简单。
改进方向：
	健壮性：处理不完整读取、时间戳连续性。
性能：
	优化休眠策略，减少延迟。
可维护性：
	添加日志和错误处理。
*/
void ScreenLive::EncodeAudio()
{
	// 初始化PCM缓冲区
	/*
	作用：
		分配一个共享指针管理的PCM缓冲区，大小为 48000 * 8 字节。
	设计意图：
		容量计算：假设采样率为48kHz，双通道，32位浮点格式（每样本4字节），则每秒数据量为 48000 * 2 * 4 = 384,000 字节，与 48000 * 8=384,000 匹配。缓冲区可容纳1秒的音频数据。
	内存安全：
		使用 std::default_delete 确保数组内存正确释放。
	*/
	std::shared_ptr<uint8_t> pcm_buffer(new uint8_t[48000 * 8], std::default_delete<uint8_t[]>());
	uint32_t frame_samples = aac_encoder_.GetFrames(); // 单次编码所需的样本数（如1024）
	uint32_t channel = audio_capture_.GetChannels();   // 声道数（如2-立体声）
	uint32_t samplerate = audio_capture_.GetSamplerate(); // 采样率（如48000Hz）
	
	// 主循环：音频采集与编码
	/*
	流程：
		检查数据量：若采集设备中的样本数足够（>= frame_samples），则读取数据。
		读取PCM数据：调用 audio_capture_.Read 读取 frame_samples 个样本到缓冲区。
		AAC编码：将PCM数据送入编码器，生成AAC数据包（AVPacketPtr）。
		推送数据：若编码成功，生成时间戳并通过 PushAudio 发送数据。
		数据不足处理：若样本不足，线程休眠1ms，减少CPU占用。
	*/
	while (is_encoder_started_)
	{		
		if (audio_capture_.GetSamples() >= (int)frame_samples) {
			// 读取PCM数据并编码
			if (audio_capture_.Read(pcm_buffer.get(), frame_samples) != frame_samples) {
				continue; // 读取失败，跳过本次循环
			}

			// AAC编码
			ffmpeg::AVPacketPtr pkt_ptr = aac_encoder_.Encode(pcm_buffer.get(), frame_samples);
			if (pkt_ptr) {
				// 生成时间戳并推送数据
				uint32_t timestamp = xop::AACSource::GetTimestamp(samplerate);
				PushAudio(pkt_ptr->data, pkt_ptr->size, timestamp);
			}
		}
		else {
			// 数据不足，休眠1ms避免忙等待
			std::this_thread::sleep_for(std::chrono::milliseconds(1));
		}
	}
}

void ScreenLive::PushVideo(const uint8_t* data, uint32_t size, uint32_t timestamp)
{
	xop::AVFrame video_frame(size);
	video_frame.size = size - 4; /* -4 去掉H.264起始码 */
	video_frame.type = IsKeyFrame(data, size) ? xop::VIDEO_FRAME_I : xop::VIDEO_FRAME_P;
	video_frame.timestamp = timestamp;
	memcpy(video_frame.buffer.get(), data + 4, size - 4);

	if (size > 0) {
		std::lock_guard<std::mutex> locker(mutex_);

		/* RTSP服务器 */
		if (rtsp_server_ != nullptr && this->rtsp_clients_.size() > 0) {
			rtsp_server_->PushFrame(media_session_id_, xop::channel_0, video_frame);
		}

		/* RTSP推流 */
		if (rtsp_pusher_ != nullptr && rtsp_pusher_->IsConnected()) {
			rtsp_pusher_->PushFrame(xop::channel_0, video_frame);
		}

		/* RTMP推流 */
		if (rtmp_pusher_ != nullptr && rtmp_pusher_->IsConnected()) {
			rtmp_pusher_->PushVideoFrame(video_frame.buffer.get(), video_frame.size);
		}
	}
}

void ScreenLive::PushAudio(const uint8_t* data, uint32_t size, uint32_t timestamp)
{
	xop::AVFrame audio_frame(size);
	audio_frame.timestamp = timestamp;
	audio_frame.type = xop::AUDIO_FRAME;
	audio_frame.size = size;
	memcpy(audio_frame.buffer.get(), data, size);

	if(size > 0){
		std::lock_guard<std::mutex> locker(mutex_);

		/* RTSP服务器 */
		if (rtsp_server_ != nullptr && this->rtsp_clients_.size() > 0) {
			rtsp_server_->PushFrame(media_session_id_, xop::channel_1, audio_frame);
		}

		/* RTSP推流 */
		if (rtsp_pusher_ && rtsp_pusher_->IsConnected()) {
			rtsp_pusher_->PushFrame(xop::channel_1, audio_frame);
		}

		/* RTMP推流 */
		if (rtmp_pusher_ != nullptr && rtmp_pusher_->IsConnected()) {
			rtmp_pusher_->PushAudioFrame(audio_frame.buffer.get(), audio_frame.size);
		}
	}
}