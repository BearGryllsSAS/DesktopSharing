#pragma once

/*
功能：
	封装多种 H.264 编码后端（NVIDIA NVENC、Intel QSV、FFmpeg 软编码），提供统一的编码接口。
​设计模式：
	采用 ​策略模式，通过 SetCodec 动态选择编码器实现。
​特性：
	不可拷贝（禁用拷贝构造函数和赋值运算符）。
	支持多编码器后端。
	隐藏硬件编码器实现细节（如 NVIDIA NVENC 使用 void* nvenc_data_）。
*/

#include "avcodec/h264_encoder.h"
#include "NvCodec/nvenc.h"
#include "QsvCodec/QsvEncoder.h"
#include <string>

class H264Encoder
{
public:
	H264Encoder& operator=(const H264Encoder&) = delete;
	H264Encoder(const H264Encoder&) = delete;
	H264Encoder();
	virtual ~H264Encoder();

	void SetCodec(std::string codec);

	bool Init(int framerate, int bitrate_kbps, int format, int width, int height);
	void Destroy();

	int Encode(uint8_t* in_buffer, uint32_t in_width, uint32_t in_height,
			   uint32_t image_size, std::vector<uint8_t>& out_frame);

	int GetSequenceParams(uint8_t* out_buffer, int out_buffer_size);

private:
	bool IsKeyFrame(const uint8_t* data, uint32_t size);

	std::string codec_;					// 当前使用的编码器类型（如 "nvenc", "qsv", "ffmpeg"）。
	ffmpeg::AVConfig encoder_config_;	// 编码配置参数（帧率、码率等），可能与 FFmpeg 相关。
	void* nvenc_data_ = nullptr;		// NVIDIA NVENC 编码器的内部数据（隐藏实现细节）。
	QsvEncoder qsv_encoder_;			// Intel Quick Sync Video（QSV）编码器的实例。
	ffmpeg::H264Encoder h264_encoder_;	// FFmpeg 软件编码器的实例。
};
