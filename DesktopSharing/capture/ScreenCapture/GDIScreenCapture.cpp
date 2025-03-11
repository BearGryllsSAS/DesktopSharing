#include "GDIScreenCapture.h"
extern "C" {
#include <libavutil/pixdesc.h>  // 提供 av_get_pix_fmt_name 函数声明
}
#include <Windows.h>

GDIScreenCapture::GDIScreenCapture()
{
	avdevice_register_all();	
}

GDIScreenCapture::~GDIScreenCapture()
{

}

/*
功能：初始化基于 FFmpeg 的屏幕捕获，配置指定显示器的区域，初始化解码器，并启动后台捕获线程。
​核心流程：参数配置 → 输入设备初始化 → 解码器设置 → 启动线程。
​适用场景：实时屏幕捕获、录制或推流，依赖 FFmpeg 处理底层复杂操作。
*/
bool GDIScreenCapture::Init(int display_index)
{
	if (is_initialized_) {
		return true;
	}

	// 获取显示器信息
	/*
	流程：
		DX::GetMonitors() 获取所有显示器信息（假设 DX::Monitor 包含显示器坐标 left/right/top/bottom）。
		检查目标显示器索引是否有效。
		保存目标显示器的坐标到 monitor_。
​	关键变量：
		monitor_.left/right/top/bottom：定义捕获区域的坐标范围。
	*/
	std::vector<DX::Monitor> monitors = DX::GetMonitors();
	if (monitors.size() < (size_t)(display_index + 1)) {
		return false;
	}

	monitor_ = monitors[display_index];


	// 配置 FFmpeg 参数
	/*
	参数详解：
		framerate：捕获帧率（framerate_ 成员变量，默认值可能为 25 FPS）。
		draw_mouse：是否绘制鼠标（1 表示绘制）。
		offset_x/offset_y：捕获区域的左上角坐标（相对于主显示器）。
		video_size：捕获区域的尺寸（如 1920x1080）。
​	注意：
		av_dict_set 的参数 video_size 必须与 offset_x/offset_y 匹配，否则可能导致捕获错位。
	*/
	char video_size[20] = { 0 };
	snprintf(video_size, sizeof(video_size), "%dx%d",
		monitor_.right - monitor_.left, monitor_.bottom - monitor_.top);

	AVDictionary *options = nullptr;
	av_dict_set_int(&options, "framerate", framerate_, AV_DICT_MATCH_CASE);
	av_dict_set_int(&options, "draw_mouse", 1, AV_DICT_MATCH_CASE);
	av_dict_set_int(&options, "offset_x", monitor_.left, AV_DICT_MATCH_CASE);
	av_dict_set_int(&options, "offset_y", monitor_.top, AV_DICT_MATCH_CASE);
	av_dict_set(&options, "video_size", video_size, 1);


	// 初始化 FFmpeg 输入设备
	/*
	​核心逻辑：
		av_find_input_format("gdigrab")：查找 FFmpeg 的 gdigrab 输入设备（Windows 屏幕捕获设备）。
		avformat_alloc_context()：分配输入流上下文 format_context_。
		avformat_open_input()：打开输入设备，参数 "desktop" 表示捕获整个屏幕，但通过 offset_x/offset_y 和 video_size 限制为指定显示器区域。
​	关键点：
		若 avformat_open_input 失败，需手动释放 format_context_（但此处未释放，可能导致内存泄漏）。
		options 参数在 avformat_open_input 调用后会被 FFmpeg 内部释放，无需手动清理。
	*/
	input_format_ = av_find_input_format("gdigrab");
	if (!input_format_) {
		printf("[GDIScreenCapture] Gdigrab not found.\n");
		return false;
	}

	format_context_ = avformat_alloc_context();
	if (avformat_open_input(&format_context_, "desktop", input_format_, &options) != 0) {
		printf("[GDIScreenCapture] Open input failed.");
		return false;
	}


	// 获取流信息
	/*
	​作用：解析输入流的信息（如视频流、音频流）。
	​失败处理：关闭输入流上下文，避免内存泄漏。
	*/
	if (avformat_find_stream_info(format_context_, nullptr) < 0) {
		printf("[GDIScreenCapture] Couldn't find stream info.\n");
		avformat_close_input(&format_context_);
		format_context_ = nullptr;
		return false;
	}


	// 查找视频流索引
	/*
	逻辑：
		遍历所有流，找到第一个视频流（AVMEDIA_TYPE_VIDEO）。
​	问题：
		若存在多个视频流（如多显示器），可能无法正确选择目标流。
	*/
	int video_index = -1;

	for (unsigned int i = 0; i < format_context_->nb_streams; i++) {
		if (format_context_->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
			video_index = i;
		}
	}

	if (video_index < 0) {
		printf("[GDIScreenCapture] Couldn't find video stream.\n");
		avformat_close_input(&format_context_);
		format_context_ = nullptr;
		return false;
	}


	// 初始化解码器
	/*
	流程：
		avcodec_find_decoder：根据视频流的编码 ID（如 AV_CODEC_ID_BMP）查找解码器。
		avcodec_alloc_context3：分配解码器上下文。
		avcodec_parameters_to_context：将流的编解码参数复制到解码器上下文。
		avcodec_open2：打开解码器。
​	关键点：
		gdigrab 默认输出原始像素（如 bgra），但此处仍初始化解码器，可能是为了后续格式转换（如转 RGB）。
		若解码器不支持原始格式（如 AV_CODEC_ID_BMP），可能导致失败。
	*/
	AVCodec* codec = avcodec_find_decoder(format_context_->streams[video_index]->codecpar->codec_id);
	if (!codec) {
		avformat_close_input(&format_context_);
		format_context_ = nullptr;
		return false;
	}

	codec_context_ = avcodec_alloc_context3(codec);
	if (!codec_context_) {
		return false;
	}

	avcodec_parameters_to_context(codec_context_, format_context_->streams[video_index]->codecpar);
	if (avcodec_open2(codec_context_, codec, nullptr) != 0) {
		avcodec_close(codec_context_);
		codec_context_ = nullptr;
		avformat_close_input(&format_context_);
		format_context_ = nullptr;
		return false;
	}


	// 完成初始化
	/*
	最终操作：
	保存视频流索引 video_index_。
	标记初始化完成（is_initialized_ = true）。
	调用 StartCapture() 启动后台线程，开始持续捕获帧。
	*/
	video_index_ = video_index;
	is_initialized_ = true;
	StartCapture();
	return true;
}

bool GDIScreenCapture::Destroy()
{
	if (is_initialized_) {
		StopCapture();		

		if (codec_context_) {
			avcodec_close(codec_context_);
			codec_context_ = nullptr;
		}

		if (format_context_) {
			avformat_close_input(&format_context_);
			format_context_ = nullptr;
		}
		
		input_format_ = nullptr;
		video_index_ = -1;
		is_initialized_ = false;
		return true;
	}
	
	return false;
}

bool GDIScreenCapture::StartCapture()
{
	if (is_initialized_ && !is_started_) {
		is_started_ = true;
		thread_ptr_.reset(new std::thread([this] {
			while (is_started_) {
				std::this_thread::sleep_for(std::chrono::milliseconds(1000 / framerate_));
				AquireFrame();
			}
		}));

		return true;
	}

	return false;
}

void GDIScreenCapture::StopCapture()
{
	if (is_started_) {
		is_started_ = false;
		thread_ptr_->join();
		thread_ptr_.reset();

		std::lock_guard<std::mutex> locker(mutex_);
		image_.reset();
		image_size_ = 0;
		width_ = 0;
		height_ = 0;
	}
}

/*
​功能：
	从 FFmpeg 输入流中读取视频数据包并解码为帧。
​关键步骤：
	使用智能指针管理 FFmpeg 对象。
	读取数据包并验证流索引。
	解码数据包并存储结果。
​应用场景：	
	实时屏幕捕获中逐帧获取画面数据，供后续处理（如显示、编码或推流）。
*/
/*
核心目标：
	从 FFmpeg 输入流（屏幕捕获设备）中读取一个视频数据包（AVPacket），解码为视频帧（AVFrame）。
​流程：
	检查捕获是否已启动。
	分配并初始化 AVPacket 和 AVFrame。
	从输入流中读取数据包。
	若数据包属于视频流，调用解码逻辑。
	释放资源并返回结果。
*/
bool GDIScreenCapture::AquireFrame()
{
	if (!is_started_) {
		return false;
	}

	// 分配智能指针管理的 FFmpeg 对象
	/*
	资源管理：
		av_frame_alloc() 创建 AVFrame，用于存储解码后的帧数据。
		av_packet_alloc() 创建 AVPacket，用于存储原始数据包。
		使用 std::shared_ptr 配合自定义删除器，确保自动释放资源：
			av_frame_free 释放 AVFrame。
			av_packet_free 释放 AVPacket。
​	初始化数据包：
		av_init_packet(av_packet.get()) 初始化 AVPacket 的字段（如 data 和 size）。
	*/
	std::shared_ptr<AVFrame> av_frame(av_frame_alloc(), [](AVFrame *ptr) {av_frame_free(&ptr);});
	std::shared_ptr<AVPacket> av_packet(av_packet_alloc(), [](AVPacket *ptr) {av_packet_free(&ptr);});
	av_init_packet(av_packet.get());

	// 从输入流中读取数据包
	/*
	关键函数：
		av_read_frame
​	作用：
		从 format_context_（关联到 gdigrab 设备）读取一个数据包。
​	返回值：
		ret >= 0：成功读取。
		ret < 0：错误或流结束（如 AVERROR_EOF）。
​	失败处理：
		直接返回 false，可能需区分错误类型（如网络中断、文件结束）。
	*/
	int ret = av_read_frame(format_context_, av_packet.get());
	if (ret < 0) {
		return false;
	}

	// 解码视频数据包
	/*
	条件检查：
		仅处理视频流的数据包（通过 stream_index 匹配 video_index_）。
​	解码操作：
		调用 Decode 方法（参数为 AVFrame 和 AVPacket），将原始数据包解码为帧。
​	假设：
		Decode 方法内部调用 avcodec_send_packet 和 avcodec_receive_frame，并将解码后的帧存储到成员变量（如 image_）。
	*/
	if (av_packet->stream_index == video_index_) {
		Decode(av_frame.get(), av_packet.get());
	}

	// 释放数据包资源
	/*
	资源释放：
		av_packet_unref 释放数据包内部的引用计数资源（如 data 缓冲区）。
	​返回值：
		true 表示成功获取并处理一帧。
	*/
	av_packet_unref(av_packet.get());
	return true;
}

/*
​功能：
	解码视频数据包并将结果存储到图像缓冲区。
​关键问题：
	缓冲区大小错误、行拷贝逻辑不严谨、解码器状态处理不当。
​改进方向：
	精确计算图像大小、处理行填充、验证像素格式、完善错误处理。
*/
/*
核心目标：
	将原始数据包（AVPacket）解码为视频帧（AVFrame），并将帧数据拷贝到类的图像缓冲区（image_）。
​流程：
	发送数据包到解码器。
	接收解码后的帧。
	将帧数据拷贝到成员变量。
	释放帧资源。

*/
bool GDIScreenCapture::Decode(AVFrame* av_frame, AVPacket* av_packet)
{
	// 发送数据包到解码器
	/*
	作用：
		将数据包送入解码器队列。
​	返回值处理：
		ret < 0：发送失败（如解码器错误、内存不足），返回 false。
​		问题：未处理 AVERROR(EAGAIN)（需要先接收已解码的帧才能继续发送）。
	*/
	int ret = avcodec_send_packet(codec_context_, av_packet);
	if (ret < 0) {
		return false;
	}

	// 接收解码后的帧
	/*
	逻辑：
	AVERROR(EAGAIN)：
		解码器需要更多数据包才能输出帧，但函数返回 true，可能误导调用方认为解码成功。
	AVERROR_EOF：
		流结束，返回 true。
​	问题：
		EAGAIN 应触发继续发送数据包，而非直接返回 true。
	*/
	if (ret >= 0) {
		ret = avcodec_receive_frame(codec_context_, av_frame);
		if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
			return true;
		}

		if (ret < 0) {
			return false;
		}

		// printf("[DEBUG] Pixel format: %s\n", av_get_pix_fmt_name(codec_context_->pix_fmt));

		std::lock_guard<std::mutex> locker(mutex_);

		// 计算图像缓冲区大小并分配内存
		image_size_ = av_frame->pkt_size;
		image_.reset(new uint8_t[image_size_], std::default_delete<uint8_t[]>());
		width_ = av_frame->width;
		height_ = av_frame->height;

		// 拷贝图像数据
		for (uint32_t i = 0; i < height_; i++) {
			memcpy(image_.get() + i * width_ * 4, av_frame->data[0] + i * av_frame->linesize[0], av_frame->linesize[0]);
		}

		av_frame_unref(av_frame);
	}

	return true;
}

bool GDIScreenCapture::CaptureFrame(std::vector<uint8_t>& image, uint32_t& width, uint32_t& height)
{
	std::lock_guard<std::mutex> locker(mutex_);

	if (!is_started_) {
		image.clear();
		return false;
	}

	if (image_ == nullptr || image_size_ == 0) {
		image.clear();
		return false;
	}

	if (image.capacity() < image_size_) {
		image.reserve(image_size_);
	}

	image.assign(image_.get(), image_.get() + image_size_);
	width = width_;
	height = height_;
	return true;
}

uint32_t GDIScreenCapture::GetWidth()  const
{
	return width_;
}

uint32_t GDIScreenCapture::GetHeight() const
{
	return height_;
}

bool GDIScreenCapture::CaptureStarted() const
{
	return is_started_;
}
