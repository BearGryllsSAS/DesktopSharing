#include "aac_encoder.h"
#include "av_common.h"

using namespace ffmpeg;

AACEncoder::AACEncoder()
{

}

AACEncoder::~AACEncoder()
{
	Destroy();
}

/*
这段代码是AAC音频编码器的初始化函数，使用FFmpeg库实现音频编码功能。

实现了以下功能：

初始化检查：防止重复初始化。
编码器配置：设置采样率、格式、声道数、码率等参数。
重采样器设置：确保输入数据格式符合编码器要求。
错误处理：在关键步骤失败时清理资源。
适用场景：需要将音频数据（如麦克风输入）编码为AAC格式的实时流媒体或文件存储。
改进方向：错误日志细化、动态重采样器启用、全局头处理。
*/
bool AACEncoder::Init(AVConfig& audio_config)
{
	if (is_initialized_) {
		return false; // 防止重复初始化
	}

	av_config_ = audio_config; // 保存配置参数

	// 查找并分配AAC编码器
	/*
	关键点：
		avcodec_find_encoder：查找FFmpeg中的AAC编码器。
		avcodec_alloc_context3：分配编码器上下文，用于配置编码参数。
		错误处理：失败时调用Destroy()释放资源。
	*/
	AVCodec *codec = nullptr;
	codec = avcodec_find_encoder(AV_CODEC_ID_AAC);
	if (!codec) {
		LOG("AAC Encoder not found.\n");
		Destroy();
		return false;
	}

	codec_context_ = avcodec_alloc_context3(codec);
	if (!codec_context_) {
		LOG("avcodec_alloc_context3() failed.");
		Destroy();
		return false;
	}

	// 配置编码器参数
	/*
	参数详解：
		采样格式：AV_SAMPLE_FMT_FLTP 表示浮点型平面格式（每个声道单独存储），是AAC编码的常用格式。
		声道布局：根据声道数自动推导（如双声道对应AV_CH_LAYOUT_STEREO）。
		码率：直接影响音频质量和文件大小（如128kbps为常见值）。
		全局头标志：注释说明未启用，SPS/PPS等元数据不会存储在extradata中。
	*/
	codec_context_->sample_rate = av_config_.audio.samplerate; // 采样率（如44100Hz）
	codec_context_->sample_fmt = AV_SAMPLE_FMT_FLTP;           // 采样格式：浮点平面格式
	codec_context_->channels = av_config_.audio.channels;      // 声道数（如2-立体声）
	codec_context_->channel_layout = av_get_default_channel_layout(av_config_.audio.channels); // 声道布局
	codec_context_->bit_rate = av_config_.audio.bitrate;       // 目标码率（如128000 bps）
	// codec_context_->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;   // 注释掉的全局头标志

	// 打开编码器
	/*
	作用：根据配置打开编码器，验证参数合法性。
	失败原因：可能包括不支持的采样率、声道数或码率。
	*/
	if (avcodec_open2(codec_context_, codec, NULL) != 0) {
		LOG("avcodec_open2() failed.\n");
		Destroy();
		return false;
	}

	// 初始化音频重采样器
	/*
	作用：
		将输入音频格式转换为编码器所需的格式（如S16→FLTP）。
	参数解析：
		输入：采样率、声道数、格式（由av_config_.audio.format定义，如AV_SAMPLE_FMT_S16）。
		输出：与输入相同的采样率和声道数，但格式为AV_SAMPLE_FMT_FLTP。
	必要性：
		确保输入数据符合编码器的格式要求。
	*/
	audio_resampler_.reset(new Resampler());
	if (!audio_resampler_->Init(av_config_.audio.samplerate, av_config_.audio.channels, 
								av_config_.audio.format, av_config_.audio.samplerate, 
								av_config_.audio.channels, AV_SAMPLE_FMT_FLTP)) {		
		LOG("Audio resampler init failed.\n");
		Destroy();
		return false;
	}

	is_initialized_ = true;
	return true;
}

void AACEncoder::Destroy()
{
	if (audio_resampler_) {
		audio_resampler_->Destroy();
		audio_resampler_.reset();
	}

	if (codec_context_) {
		avcodec_close(codec_context_);
		avcodec_free_context(&codec_context_);
		codec_context_ = nullptr;
	}

	pts_ = 0;
	is_initialized_ = false;
}

uint32_t AACEncoder::GetFrameSamples()
{
	if (is_initialized_) {
		return codec_context_->frame_size;
	}

	return 0;
}

AVPacketPtr AACEncoder::Encode(const uint8_t* pcm, int samples)
{
	AVFramePtr in_frame(av_frame_alloc(), [](AVFrame* ptr) {
		av_frame_free(&ptr);
	});

	in_frame->sample_rate = codec_context_->sample_rate;
	in_frame->format = AV_SAMPLE_FMT_FLT;
	in_frame->channels = codec_context_->channels;
	in_frame->channel_layout = codec_context_->channel_layout;
	in_frame->nb_samples = samples;
	in_frame->pts = pts_;
	in_frame->pts = av_rescale_q(pts_, { 1, codec_context_->sample_rate }, codec_context_->time_base);
	pts_ += in_frame->nb_samples;

	if (av_frame_get_buffer(in_frame.get(), 0) < 0) {
		LOG("av_frame_get_buffer() failed.\n");
		return nullptr;
	}

	int bytes_per_sample = av_get_bytes_per_sample(av_config_.audio.format);
	if (bytes_per_sample == 0) {
		return nullptr;
	}

	memcpy(in_frame->data[0], pcm, bytes_per_sample * in_frame->channels * samples);

	AVFramePtr fltp_frame = nullptr;
	if (audio_resampler_->Convert(in_frame, fltp_frame) <= 0) {
		return nullptr;
	}

	int ret = avcodec_send_frame(codec_context_, fltp_frame.get());
	if (ret != 0) {
		return nullptr;
	}
	
	AVPacketPtr av_packet(av_packet_alloc(), [](AVPacket* ptr) {av_packet_free(&ptr);});
	av_init_packet(av_packet.get());

	ret = avcodec_receive_packet(codec_context_, av_packet.get());
	if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
		return nullptr;
	}
	else if (ret < 0) {
		LOG("avcodec_receive_packet() failed.");
		return nullptr;
	}

	return av_packet;
}
