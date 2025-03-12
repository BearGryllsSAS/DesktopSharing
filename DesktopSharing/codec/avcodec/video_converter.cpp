#include "video_converter.h"

using namespace ffmpeg;

VideoConverter::VideoConverter()
{

}

VideoConverter::~VideoConverter()
{
	Destroy();
}

bool VideoConverter::Init(int in_width, int in_height, AVPixelFormat in_format,
	int out_width, int out_height, AVPixelFormat out_format)
{
	if (sws_context_) {
		return false;
	}

	if (sws_context_ == nullptr) {
		sws_context_ = sws_getContext(
			in_width, in_height, in_format,out_width, 
			out_height, out_format, 
			SWS_BICUBIC, NULL, NULL, NULL);

		out_width_ = out_width;
		out_height_ = out_height;
		out_format_ = out_format;
		return sws_context_ != nullptr;
	}
	return false;
}

void VideoConverter::Destroy()
{
	if (sws_context_) {
		sws_freeContext(sws_context_);
		sws_context_ = nullptr;
	}
}

/*
输入与输出：

​输入帧：in_frame（原始视频帧，类型为 AVFramePtr，可能是智能指针）。
​输出帧：out_frame（转换后的视频帧，通过引用传递，避免拷贝开销）。

​
核心操作：

检查是否初始化了缩放/格式转换的上下文 sws_context_。
为输出帧分配内存并设置参数（分辨率、格式、时间戳等）。
调用 sws_scale 进行实际的图像转换。
处理错误（如内存分配失败、转换失败）。
*/
int VideoConverter::Convert(AVFramePtr in_frame, AVFramePtr& out_frame)
{
	// 检查 sws_context_ 是否存在
	/*
	​目的：
		sws_context_ 是 FFmpeg 的 SwsContext 结构体，用于管理图像缩放和格式转换的上下文。
​	逻辑：
		如果未初始化（例如未调用 sws_init），直接返回错误码 -1，避免后续操作崩溃。
	*/
	if (!sws_context_) {
		return -1;
	}

	// 分配输出帧内存
	/*
	目的：
		为输出帧 out_frame 分配内存，并通过智能指针管理生命周期。
​	细节：
		av_frame_alloc()：FFmpeg API，分配一个空的 AVFrame。
		reset()：智能指针（如 std::shared_ptr）的方法，释放旧对象并接管新对象。
		Lambda 删除器：确保通过 av_frame_free 正确释放 AVFrame，避免内存泄漏。
	*/
	out_frame.reset(av_frame_alloc(), [](AVFrame* ptr) {
		av_frame_free(&ptr);
	});

	// 设置输出帧参数
	/*
	​目的：
		配置输出帧的分辨率、像素格式和时间戳。
​	参数来源：
		out_width_/out_height_/out_format_：类成员变量，表示目标分辨率（如 1920x1080）和像素格式（如 AV_PIX_FMT_YUV420P）。
		pts（显示时间戳）和 pkt_dts（解码时间戳）：从输入帧复制，保证时间信息连续性。
	*/
	out_frame->width = out_width_;
	out_frame->height = out_height_;
	out_frame->format = out_format_;
	out_frame->pts = in_frame->pts;
	out_frame->pkt_dts = in_frame->pkt_dts;

	// 分配输出帧缓冲区
	/*
	目的：
		为输出帧分配实际存储图像数据的缓冲区。
​	参数：
		out_frame.get()：获取底层 AVFrame 裸指针。
		32：内存对齐参数（如 32 字节对齐），提升内存访问效率。
​		返回值：若分配失败（如内存不足），返回 -1。
	*/
	if (av_frame_get_buffer(out_frame.get(), 32) != 0) {
		return -1;
	}

	// 执行图像转换
	/*
	目的：
		调用 sws_scale 完成实际的图像缩放和格式转换。
​	关键参数：
		in_frame->data 和 out_frame->data：分别指向输入/输出帧的像素数据（如 YUV 的三个平面）。
		in_frame->linesize 和 out_frame->linesize：每行的字节数（考虑内存对齐后的值）。
​	返回值：
		成功时返回输出帧的高度（应等于 out_height_）。
		失败时返回负数，代码中捕获并返回 -1。
	*/
	int out_height = sws_scale(
		sws_context_,           // SwsContext 上下文
		in_frame->data,         // 输入帧的数据指针数组
		in_frame->linesize,     // 输入帧每行的字节数数组
		0,                      // 输入帧的起始行（从第 0 行开始处理）
		in_frame->height,       // 输入帧的高度（处理全部行）
		out_frame->data,        // 输出帧的数据指针数组
		out_frame->linesize     // 输出帧每行的字节数数组
	);

	if (out_height < 0) {
		return -1;
	}

	return out_height;
}