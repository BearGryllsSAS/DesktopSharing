#include "h264_encoder.h"
#include "av_common.h"

#define USE_LIBYUV 0
#if USE_LIBYUV
#include "libyuv.h"
#endif

using namespace ffmpeg;

/*
初始化基于 FFmpeg libx264 的 H.264 编码器，配置编码参数（分辨率、帧率、码率、GOP 等），并设置低延迟优化选项。

功能：
	通过 libx264 实现了一个低延迟的 H.264 编码器，适用于实时视频处理。
​注意事项：
	输入格式需为 YUV420P，码率控制需验证，错误处理可进一步优化。
​扩展性：
	可通过动态选择编码器和调整参数适配更多场景。
*/

/*

关键参数：

编码器			libx264				FFmpeg 的软件 H.264 编码器
像素格式		YUV420P				输入数据需转换为此格式
GOP				用户配置			关键帧间隔（帧数），影响容错性和压缩率
码率控制模式	CBR					恒定码率（需验证参数合理性）
Preset			ultrafast			编码速度优先，压缩率较低
Tune			zerolatency			最小化编码延迟，适合实时场景
B帧				禁用				减少延迟，但压缩效率降低
*/
bool H264Encoder::Init(AVConfig& video_config)
{
	if (is_initialized_) {
		Destroy();
	}

	av_config_ = video_config;

	//av_log_set_level(AV_LOG_DEBUG);

	// 查找编码器
	/*
	关键点：
		显式使用 libx264（软件编码器），而非系统默认的 H.264 编码器。
		若未编译或加载 libx264，初始化失败。
	*/
	AVCodec *codec = nullptr;
	//codec = avcodec_find_encoder(AV_CODEC_ID_H264);
	codec = avcodec_find_encoder_by_name("libx264");
	if (!codec) {
		LOG("H.264 Encoder not found.\n");
		Destroy();
		return false;
	}

	// 分配编码器上下文
	codec_context_ = avcodec_alloc_context3(codec);
	if (!codec_context_) {
		LOG("avcodec_alloc_context3() failed.");
		Destroy();
		return false;
	}
	
	// 配置基础参数
	/*
	​GOP：
		关键帧间隔，直接影响容错性和压缩率（如 gop=30 表示每30帧一个关键帧）。
	​B帧禁用：
		max_b_frames=0 减少编码延迟，适合实时场景。
​	像素格式：
		输入必须为 YUV420P，需确保数据源格式匹配。
	*/
	codec_context_->width = av_config_.video.width;							// 视频宽度
	codec_context_->height = av_config_.video.height;						// 视频高度
	codec_context_->time_base = { 1, (int)av_config_.video.framerate };		// 时间基（单位：秒）
	codec_context_->framerate = { (int)av_config_.video.framerate, 1 };		// 帧率（分子/分母）
	codec_context_->gop_size = av_config_.video.gop;						// 关键帧间隔（GOP）
	codec_context_->max_b_frames = 0;										// 禁用B帧
	codec_context_->pix_fmt = AV_PIX_FMT_YUV420P;							// 输入像素格式

	/*
	禁用B帧的优势：	​			禁用B帧的代价：
	
	降低编码/解码延迟			压缩效率下降（码率升高）
	减少解码计算资源消耗	
	码率控制更稳定	
	兼容性更好	
	逻辑简化，实现更简单
	*/

	//======================================================================================================CBR
	//======================================================================================================CBR
	//======================================================================================================CBR
	// 码率控制（CBR模式）
	/*
	模式：
		强制恒定码率（CBR），但参数设置可能不符合最佳实践。
​	问题：
		rc_buffer_size 设置为码率值，可能导致缓冲区溢出或欠载（通常应设为 2 * bit_rate）。
	*/
	// rc control mode: abr
	codec_context_->bit_rate = av_config_.video.bitrate;	// 目标码率（bps），编码器尽量让输出码率接近该值。
	
	// cbr mode config
	codec_context_->rc_min_rate = av_config_.video.bitrate; // 最小码率，码率波动的最小和最大值。在CBR模式下，二者需等于 bit_rate，强制码率恒定。
	codec_context_->rc_max_rate = av_config_.video.bitrate; // 最大码率，码率波动的最小和最大值。在CBR模式下，二者需等于 bit_rate，强制码率恒定。
	codec_context_->rc_buffer_size = 2 * (int)av_config_.video.bitrate; // 缓冲区大小，虚拟缓冲区大小（单位：比特），用于平滑码率波动。编码器会根据缓冲区填充状态动态调整量化参数（QP），确保缓冲区不溢出或欠载。

	/*
	​CBR模式参数设置的目的:

​	1. 参数作用解析
​		**bit_rate**：目标码率（单位：bps），编码器尽量让输出码率接近该值。
​		**rc_min_rate 和 rc_max_rate**：码率波动的最小和最大值。在CBR模式下，二者需等于 bit_rate，强制码率恒定。
​		**rc_buffer_size**：虚拟缓冲区大小（单位：比特），用于平滑码率波动。编码器会根据缓冲区填充状态动态调整量化参数（QP），确保缓冲区不溢出或欠载。
​	
	2. 为什么需要设置这些参数？
​		严格CBR需求：在直播、实时通信等场景中，网络带宽通常固定。设置 rc_min_rate = rc_max_rate = bit_rate 可强制编码器输出码率严格恒定，避免因码率波动导致网络拥塞或卡顿。
​		缓冲区容量合理性：rc_buffer_size 控制码率的短期波动容忍度。若设置过小：
		缓冲区快速填满或排空，编码器频繁调整QP，导致视频质量不稳定（忽高忽低）。
		可能触发“缓冲区欠载”（数据不足）或“溢出”（数据过多），影响解码流畅性。
​	
	3. 代码中的问题
		codec_context_->rc_buffer_size = av_config_.video.bitrate; // 问题：缓冲区大小 = 码率值
​		合理值应为 2 * bit_rate：
		假设视频帧率为30 FPS，bit_rate=3 Mbps，则每帧的平均码率为 3,000,000 / 30 = 100,000 bits。
		若 rc_buffer_size=3 Mbps，缓冲区可容纳 3,000,000 / 100,000 = 30帧 的数据，相当于1秒的缓冲。
		但实际场景中，瞬时码率可能因画面复杂度波动，通常建议 rc_buffer_size = 2 * bit_rate，平衡延迟与稳定性。
​	
	4. 如果不设置这些参数会怎样？
​		1. 默认行为
			FFmpeg的 libx264 编码器默认使用 ​VBR（可变码率）​ 模式，而非CBR。
			即使设置 bit_rate，若未显式指定 rc_min_rate 和 rc_max_rate，编码器仍可能根据画面复杂度动态调整码率。
​		2. 实现CBR必须显式设置
​			严格CBR：需同时设置：
				codec_context_->rc_min_rate = codec_context_->rc_max_rate = codec_context_->bit_rate;
				codec_context_->rc_buffer_size = 2 * codec_context_->bit_rate; // 推荐值
​			仅设置 bit_rate：实际是 ​VBR模式，码率可能在目标值附近波动。
​	
	5. 能否省略这一步？
​		1. 省略的后果
​			无法实现严格CBR：码率仍有波动，可能超出网络带宽限制。
​			缓冲区默认值不可控：不同编码器的默认 rc_buffer_size 可能不适用于实时场景。
​		2. 哪些场景可以省略？
​			非实时场景​（如视频文件存储）：使用VBR模式可提高压缩率。
​			对码率波动不敏感的网络​（如高速局域网）。
​	
​	6. 总结
​		操作：				​	必要性：				​	后果：
		设置 bit_rate			必需						目标码率基准
		设置 rc_min/max_rate	必需（严格CBR）				强制码率恒定
		设置 rc_buffer_size		强烈建议（2倍码率）			平衡码率波动与延迟
		完全省略				仅适合非实时场景			实际为VBR模式，码率波动
		
	7. 结论：在实时视频传输中，必须显式设置CBR参数以确保码率严格恒定，同时合理配置 rc_buffer_size 避免缓冲区问题。
	*/


	//======================================================================================================CBR
	//======================================================================================================CBR
	//======================================================================================================CBR


	//======================================================================================================全局头信息
	//======================================================================================================全局头信息
	//======================================================================================================全局头信息
	// 全局头信息	​作用：生成包含 SPS/PPS 的全局头，用于流媒体协议（如 RTMP、RTSP）。
	codec_context_->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;

	/*
	注意事项
		​必须设置该标志的场景：所有需要实时传输的流媒体协议（如 RTMP、RTSP、WebRTC）。
		​无需设置该标志的场景：本地视频文件保存（如 MP4、MKV），此时 SPS / PPS 会写入文件头。
		​兼容性：部分硬件编码器可能不支持此标志，需查阅具体编码器文档。
	总结
		设置 AV_CODEC_FLAG_GLOBAL_HEADER 的目的是将 SPS / PPS 从视频帧中分离，
		存储到全局头信息（extradata）中，使流媒体客户端能在播放前初始化解码器。这是实时视频传输的关键步骤，确保低延迟和兼容性。
	*/

	//======================================================================================================全局头信息
	//======================================================================================================全局头信息
	//======================================================================================================全局头信息

	// 编码器私有参数优化
	/*
	​参数说明：
	​preset: 
		ultrafast 牺牲压缩率以换取最高编码速度。
​	tune: 
		zerolatency 减少编码缓冲，降低端到端延迟。
​	forced-idr: 
		允许外部触发关键帧（如推流时应对网络丢包）。

	*/
	if (codec->id == AV_CODEC_ID_H264) {
		av_opt_set(codec_context_->priv_data, "preset", "ultrafast", 0); //ultrafast 
	}

	av_opt_set(codec_context_->priv_data, "tune", "zerolatency", 0);
	av_opt_set_int(codec_context_->priv_data, "forced-idr", 1, 0);
	av_opt_set_int(codec_context_->priv_data, "avcintra-class", -1, 0);
	
	// 打开编码器
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

	//============================================================================关键帧
	//============================================================================关键帧
	//============================================================================关键帧
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
	1. I帧（关键帧）的生成
​		(1) 自动生成（基于GOP设置）​
​			GOP（Group of Pictures）​：通过 codec_context_->gop_size 参数设置。
			
			codec_context_->gop_size = av_config_.video.gop;  // 例如：gop=30 → 每30帧一个I帧
			
			编码器会每隔 gop_size 帧自动生成一个I帧，形成类似 IPPP...IPPP 的序列。
			这里的 I 帧实际是 ​IDR帧​（解码器可安全重置的I帧）。
​		
		(2) 强制生成（手动触发）​
​			外部触发：通过设置 force_idr_ 标志，强制生成I帧。

			if (force_idr_) {
			    yuv_frame->pict_type = AV_PICTURE_TYPE_I;  // 强制生成IDR帧
			    force_idr_ = false;
			}

​			应用场景：观众加入直播、网络丢包恢复等需要立即刷新画面的情况。
​	
	2. P帧和B帧的生成
​		(1) B帧的禁用
​			显式禁止B帧：通过 codec_context_->max_b_frames = 0。

			codec_context_->max_b_frames = 0;  // 禁用B帧

​			结果：编码器仅生成 ​I帧和P帧，无B帧。
​			原因：B帧需要参考未来帧，增加编码延迟，不适合实时场景（如直播）。

​		(2) P帧的自动生成
​			默认行为：在非I帧的位置，编码器自动生成P帧。
			P帧参考前一帧（I或P帧）进行压缩，压缩率低于B帧但延迟更低。
​	
	3. 帧类型分配的控制逻辑
​		帧类型：				​生成方式：
​		I帧					- 自动：每隔 gop_size 帧生成一个IDR帧。
							- 手动：外部设置 force_idr_ 标志触发。
​		
		P帧					自动：在非I帧的位置，编码器默认生成P帧。
​		
		B帧					自动：但在此代码中被显式禁用（max_b_frames=0）。
​	
	4. 编码器内部决策
​		运动估计与参考帧选择：
		编码器会根据以下参数自动决策P帧的压缩方式：
​		参考帧数量​（如 refs=1）：默认参考前一帧。
​		运动估计算法​（如全搜索、六边形搜索等）。
​		码率控制模式​（CBR/VBR/CRF）。
​		代码中的关键参数：

		av_opt_set(codec_context_->priv_data, "preset", "ultrafast", 0); // 编码速度优先
		av_opt_set(codec_context_->priv_data, "tune", "zerolatency", 0); // 零延迟优化
​	
	5. 总结
​		I帧：由 ​GOP间隔 + 手动触发 控制。
​		P帧：编码器自动生成，无B帧。
​		B帧：显式禁用（max_b_frames=0）。
​		帧序列示例：
		I P P P ... I P P P（无B帧，适合低延迟场景）。
	*/

	//============================================================================关键帧
	//============================================================================关键帧
	//============================================================================关键帧

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