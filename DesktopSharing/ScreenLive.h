#ifndef SCREEN_LIVE_H
#define SCREEN_LIVE_H

/*
ScreenLive 是一个集成了屏幕捕获、视频和音频编码、流媒体推送等多种功能的类，适用于实时视频流的捕获和推送。
它通过封装 RTSP 和 RTMP 推流协议，使得屏幕共享和直播变得更加简便。
该类通过多线程方式进行编码和推流，确保了高效的性能和流畅的操作。
*/
 
#include "xop/RtspServer.h"
#include "xop/RtspPusher.h"
#include "xop/RtmpPublisher.h"
#include "AACEncoder.h"
#include "H264Encoder.h"
#include "AudioCapture/AudioCapture.h"
#include "ScreenCapture/ScreenCapture.h"
#include <mutex>
#include <atomic>
#include <string>
#include <set>

#define SCREEN_LIVE_RTSP_SERVER 1
#define SCREEN_LIVE_RTSP_PUSHER 2
#define SCREEN_LIVE_RTMP_PUSHER 3

// 配置音视频流的编码参数
struct AVConfig
{
	uint32_t bitrate_bps = 8000000;
	uint32_t framerate = 25;
	//uint32_t gop = 25;

	// 编码器的名称
	std::string codec = "x264"; // [software codec: "x264"]  [hardware codec: "h264_nvenc, h264_qsv"]

	bool operator != (const AVConfig &src) const {
		if (src.bitrate_bps != bitrate_bps || src.framerate != framerate ||
			src.codec != codec) {
			return true;
		}
		return false;
	}
};

// 配置流媒体推送的相关信息
struct LiveConfig
{
	// pusher
	std::string rtsp_url;
	std::string rtmp_url;

	// server
	std::string suffix; // 服务器的路径后缀
	std::string ip;
	uint16_t port;
};

class ScreenLive
{
public:
	ScreenLive & operator=(const ScreenLive &) = delete;
	ScreenLive(const ScreenLive &) = delete;
	// 单例模式静态实例化
	static ScreenLive& Instance();
	~ScreenLive();

	// 初始化与销毁
	bool Init(AVConfig& config);
	void Destroy();
	bool IsInitialized() { return is_initialized_; };

	// 启动和停止屏幕捕获过程。
	int StartCapture();
	int StopCapture();

	// 启动和停止视频和音频编码器
	int StartEncoder(AVConfig& config);
	int StopEncoder();
	bool IsEncoderInitialized() { return is_encoder_started_; };

	// 根据流类型启动和停止推流
	bool StartLive(int type, LiveConfig& config);
	void StopLive(int type);
	bool IsConnected(int type);

	// 获取当前屏幕捕获的图像，返回 BGRA 格式的图像数据，width 和 height 分别为图像的宽度和高度。
	bool GetScreenImage(std::vector<uint8_t>& bgra_image, uint32_t& width, uint32_t& height);

	// 获取系统的状态信息，如编码帧率、连接的客户端等。
	std::string GetStatusInfo();

private:
	ScreenLive();
	
	void EncodeVideo();
	void EncodeAudio();
	void PushVideo(const uint8_t* data, uint32_t size, uint32_t timestamp);
	void PushAudio(const uint8_t* data, uint32_t size, uint32_t timestamp);
	bool IsKeyFrame(const uint8_t* data, uint32_t size);

	bool is_initialized_ = false;
	bool is_capture_started_ = false;
	bool is_encoder_started_ = false;

	AVConfig av_config_;
	std::mutex mutex_;

	// capture
	ScreenCapture* screen_capture_ = nullptr;
	AudioCapture audio_capture_;

    // encoder
	H264Encoder h264_encoder_;
	AACEncoder aac_encoder_;
	std::shared_ptr<std::thread> encode_video_thread_ = nullptr;
	std::shared_ptr<std::thread> encode_audio_thread_ = nullptr;

	// streamer
	xop::MediaSessionId media_session_id_ = 0;
	std::unique_ptr<xop::EventLoop> event_loop_ = nullptr;
	std::shared_ptr<xop::RtspServer> rtsp_server_ = nullptr;
	std::shared_ptr<xop::RtspPusher> rtsp_pusher_ = nullptr;
	std::shared_ptr<xop::RtmpPublisher> rtmp_pusher_ = nullptr;

	// status info
	std::atomic_int encoding_fps_;
	std::set<std::string> rtsp_clients_;
};

#endif
