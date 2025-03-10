#include "h264_encoder.h"
#include "av_common.h"

#define USE_LIBYUV 0
#if USE_LIBYUV
#include "libyuv.h"
#endif

using namespace ffmpeg;

bool H264Encoder::Init(AVConfig& video_config)
{
	if (is_initialized_) {
		Destroy();
	}

	av_config_ = video_config;

	//av_log_set_level(AV_LOG_DEBUG);

	AVCodec *codec = nullptr;
	//codec = avcodec_find_encoder(AV_CODEC_ID_H264);
	codec = avcodec_find_encoder_by_name("libx264");
	if (!codec) {
		LOG("H.264 Encoder not found.\n");
		Destroy();
		return false;
	}

	codec_context_ = avcodec_alloc_context3(codec);
	if (!codec_context_) {
		LOG("avcodec_alloc_context3() failed.");
		Destroy();
		return false;
	}
	
	codec_context_->width = av_config_.video.width;
	codec_context_->height = av_config_.video.height;
	codec_context_->time_base = { 1,  (int)av_config_.video.framerate };
	codec_context_->framerate = { (int)av_config_.video.framerate, 1 };
	codec_context_->gop_size = av_config_.video.gop;
	codec_context_->max_b_frames = 0;
	codec_context_->pix_fmt = AV_PIX_FMT_YUV420P;

	// rc control mode: abr
	codec_context_->bit_rate = av_config_.video.bitrate;
	
	// cbr mode config
	codec_context_->rc_min_rate = av_config_.video.bitrate;
	codec_context_->rc_max_rate = av_config_.video.bitrate;
	codec_context_->rc_buffer_size = (int)av_config_.video.bitrate;

	codec_context_->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;

	if (codec->id == AV_CODEC_ID_H264) {
		av_opt_set(codec_context_->priv_data, "preset", "ultrafast", 0); //ultrafast 
	}

	av_opt_set(codec_context_->priv_data, "tune", "zerolatency", 0);
	av_opt_set_int(codec_context_->priv_data, "forced-idr", 1, 0);
	av_opt_set_int(codec_context_->priv_data, "avcintra-class", -1, 0);
	
	if (avcodec_open2(codec_context_, codec, NULL) != 0) {
		LOG("avcodec_open2() failed.\n");
		Destroy();
		return false;
	}

	in_width_ = av_config_.video.width;
	in_height_ = av_config_.video.height;
	is_initialized_ = true;
	return true;
}

void H264Encoder::Destroy()
{
	if (video_converter_) {
		video_converter_->Destroy();
		video_converter_.reset();
	}

	if (codec_context_) {
		avcodec_close(codec_context_);
		avcodec_free_context(&codec_context_);
		codec_context_ = nullptr;
	}
		
	in_width_ = 0;
	in_height_ = 0;
	pts_ = 0;
	is_initialized_ = false;
}

/*
这段代码是H.264编码器的核心编码函数，负责将输入的图像数据转换为YUV格式并编码为H.264视频流。

这段代码实现了以下功能：

输入处理：接收原始图像数据，转换为编码器所需的YUV格式。
动态转换：根据输入分辨率变化重建视频转换器。
编码控制：支持强制关键帧生成和时间戳管理。
编码输出：通过FFmpeg API生成H.264数据包。
改进方向：输入格式兼容性、错误日志增强、多线程安全。
*/
/*
输入：原始图像数据（如屏幕捕获的BGRA格式）。
输出：编码后的H.264数据包（AVPacketPtr，智能指针封装）。
*/
AVPacketPtr H264Encoder::Encode(const uint8_t *image, uint32_t width, uint32_t height, uint32_t image_size, uint64_t pts)
{
	/*
	const uint8_t *image,     输入图像数据指针（如RGB/BGRA）
    uint32_t width,           输入图像宽度
    uint32_t height,          输入图像高度
    uint32_t image_size,      输入图像数据大小
    uint64_t pts              显示时间戳（-1表示自动生成）
	*/

	// 编码器未初始化，直接返回
	if (!is_initialized_) {
		return nullptr;
	}

	/*
	作用：
		当输入分辨率变化或转换器未初始化时，重新创建VideoConverter。
		初始化转换器，将输入格式（如BGRA）转换为编码器所需的格式（如YUV420P）。
	潜在问题：
		条件中的av_config_.video.height可能是笔误，应为in_height_，否则可能导致不必要的转换器重建。
	*/
	if (width != in_width_ || height != av_config_.video.height || !video_converter_) {
		in_width_ = width;
		in_height_ = height;

		// (AVPixelFormat)av_config_.video.format	--->	AV_PIX_FMT_BGRA
		// codec_context_->pix_fmt	--->	pix_fmt
		video_converter_.reset(new ffmpeg::VideoConverter());
		if (!video_converter_->Init(in_width_, in_height_, (AVPixelFormat)av_config_.video.format,
									codec_context_->width, codec_context_->height, codec_context_->pix_fmt)) {
			video_converter_.reset();
			return nullptr;
		}
	}

	// 输入帧（AVFrame）准备
	ffmpeg::AVFramePtr in_frame(av_frame_alloc(), [](AVFrame* ptr) { av_frame_free(&ptr); });
	in_frame->width = in_width_;
	in_frame->height = in_height_;
	in_frame->format = av_config_.video.format;	// 输入像素格式 AV_PIX_FMT_BGRA
	if (av_frame_get_buffer(in_frame.get(), 32) != 0) {
		return nullptr;	// 内存分配失败
	}

	//int pic_size = av_image_get_buffer_size(av_config_.video.format, in_width_, in_height_, in_frame->linesize[0]);
	//if (pic_size <= 0) {
	//	return nullptr;
	//}

	memcpy(in_frame->data[0], image, image_size);	// 拷贝输入数据到in_frame(AVFrame)

	/*
	作用：
		分配并填充输入帧（AVFrame），存储原始图像数据。
	关键点：
		假设输入数据为打包格式（如BGRA），直接拷贝到data[0]。
		若输入为平面格式（如YUV420P），需分别填充data[0]、data[1]、data[2]。
	*/

//#if USE_LIBYUV	
	//int result = libyuv::ARGBToI420(bgra_image, width * 4,
	//	yuv_frame->data[0], yuv_frame->linesize[0],
	//	yuv_frame->data[1], yuv_frame->linesize[1],
	//	yuv_frame->data[2], yuv_frame->linesize[2],
	//	width, height);

	//if (result != 0) {
	//	LOG("libyuv::ARGBToI420() failed.\n");
	//	return nullptr;
	//}
//#else

	// 颜色空间转换（RGB/BGRA → YUV）
	/*
	作用：
		将输入帧转换为编码器所需的YUV格式（如YUV420P）。
	实现方式：
		可能使用FFmpeg的sws_scale或自定义转换逻辑。
		输出yuv_frame的格式由codec_context_->pix_fmt决定。
	*/
	AVFramePtr yuv_frame = nullptr;
	if (video_converter_->Convert(in_frame, yuv_frame) <= 0) {
		return nullptr;
	}
//#endif

	/*
	作用：为帧分配显示时间戳，确保解码顺序正确。
	注意：自动生成的pts_需初始化为0，并防止溢出。
	*/
	if (pts >= 0) {
		yuv_frame->pts = pts;	// 使用传入的PTS
	}
	else {
		yuv_frame->pts = pts_++;	 // 自动生成递增PTS
	}

	// 强制关键帧（IDR帧）
	/*
	作用：通过force_idr_标志强制生成关键帧（如推流时观众加入）。
	机制：设置pict_type为AV_PICTURE_TYPE_I，编码器生成IDR帧。
	*/
	yuv_frame->pict_type = AV_PICTURE_TYPE_NONE;
	if (force_idr_) {
		yuv_frame->pict_type = AV_PICTURE_TYPE_I;
		force_idr_ = false;
	}

	/*
	编码帧

	作用：
		将YUV帧送入编码器，接收编码后的H.264数据包。
	关键点：
		avcodec_send_frame和avcodec_receive_packet是FFmpeg的异步编码API。
		可能需要多次调用avcodec_receive_packet以获取所有待处理数据包。
	*/
	if (avcodec_send_frame(codec_context_, yuv_frame.get()) < 0) {
		LOG("avcodec_send_frame() failed.\n");
		return nullptr;
	}

	AVPacketPtr av_packet(av_packet_alloc(), [](AVPacket* ptr) {
		av_packet_free(&ptr);
	});
	av_init_packet(av_packet.get());

	int ret = avcodec_receive_packet(codec_context_, av_packet.get());
	if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
		return nullptr;	// 无数据或编码结束
	}
	else if (ret < 0) {
		LOG("avcodec_receive_packet() failed.");
		return nullptr;
	}

	return av_packet;
}

void H264Encoder::ForceIDR()
{
	if (codec_context_) {
		force_idr_ = true;		
	}
}

void H264Encoder::SetBitrate(uint32_t bitrate_kbps)
{
	if (codec_context_) {
		int64_t out_val = 0;
		av_opt_get_int(codec_context_->priv_data, "avcintra-class", 0, &out_val);
		if (out_val < 0) {
			codec_context_->bit_rate = bitrate_kbps * 1000;
			codec_context_->rc_min_rate = codec_context_->bit_rate;
			codec_context_->rc_max_rate = codec_context_->bit_rate;
			codec_context_->rc_buffer_size = (int)codec_context_->bit_rate;
		}	
	}
}