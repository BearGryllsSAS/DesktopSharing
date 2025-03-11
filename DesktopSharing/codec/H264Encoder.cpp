#include "H264Encoder.h"

H264Encoder::H264Encoder()
{
	codec_ = "x264";
}

H264Encoder::~H264Encoder()
{

}

void H264Encoder::SetCodec(std::string codec)
{
	codec_ = codec;
}

/*
初始化 H.264 编码器，支持三种后端：

​	FFmpeg 软件编码​（默认必选）
​	NVIDIA NVENC 硬件编码​（需显式指定 "h264_nvenc"）
​	Intel QSV 硬件编码​（需显式指定 "h264_qsv"）

若指定硬件编码器失败，仍会保留已初始化的软件编码器，但逻辑上可能返回 true，导致上层误认为初始化成功。
*/
bool H264Encoder::Init(int framerate, int bitrate_kbps, int format, int width, int height)
{
	encoder_config_.video.framerate = framerate;
	encoder_config_.video.bitrate = bitrate_kbps * 1000;
	encoder_config_.video.gop = framerate;
	encoder_config_.video.format = (AVPixelFormat)format;
	encoder_config_.video.width = width;
	encoder_config_.video.height = height;

	if (!h264_encoder_.Init(encoder_config_)) {
		return false;
	}

	if (codec_ == "h264_nvenc") {
		if (nvenc_info.is_supported()) {
			nvenc_data_ = nvenc_info.create();
		}

		if (nvenc_data_ != nullptr) {
			nvenc_config nvenc_config;
			nvenc_config.codec = "h264";
			nvenc_config.format = DXGI_FORMAT_B8G8R8A8_UNORM;
			nvenc_config.width = encoder_config_.video.width;
			nvenc_config.height = encoder_config_.video.height;
			nvenc_config.framerate = encoder_config_.video.framerate;
			nvenc_config.gop = encoder_config_.video.gop;
			nvenc_config.bitrate = encoder_config_.video.bitrate;
			if (!nvenc_info.init(nvenc_data_, &nvenc_config)) {
				nvenc_info.destroy(&nvenc_data_);
				nvenc_data_ = nullptr;
			}
		}
	}
	else if (codec_ == "h264_qsv") {
		if (QsvEncoder::IsSupported()) {
			QsvParams qsv_params;
			qsv_params.codec = "h264";
			qsv_params.bitrate_kbps = encoder_config_.video.bitrate / 1000;
			qsv_params.framerate = encoder_config_.video.framerate;
			qsv_params.gop = encoder_config_.video.gop;
			qsv_params.width = encoder_config_.video.width;
			qsv_params.height = encoder_config_.video.height;
			if (!qsv_encoder_.Init(qsv_params)) {
				qsv_encoder_.Destroy();
			}
		}
	}

	return true;
}

void H264Encoder::Destroy()
{
	if (nvenc_data_ != nullptr) {
		nvenc_info.destroy(&nvenc_data_);
		nvenc_data_ = nullptr;
	}

	if (qsv_encoder_.IsInitialized()) {
		qsv_encoder_.Destroy();
	}

	h264_encoder_.Destroy();
}

bool H264Encoder::IsKeyFrame(const uint8_t* data, uint32_t size)
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

int H264Encoder::Encode(uint8_t* in_buffer, uint32_t in_width, uint32_t in_height,
						uint32_t image_size, std::vector<uint8_t>& out_frame)
{
	// 清空输出缓冲区。
	out_frame.clear();

	// 检查编码器上下文是否存在。
	if (!h264_encoder_.GetAVCodecContext()) {
		return -1;
	}

	// 分配临时输出缓冲区，大小为width*height*4（假设为RGBA/BGRA格式的保守估计）。
	int frame_size = 0;
	int max_buffer_size = encoder_config_.video.width * encoder_config_.video.height * 4;
	std::shared_ptr<uint8_t> out_buffer(new uint8_t[max_buffer_size], std::default_delete<uint8_t[]>());

	if (nvenc_data_ != nullptr) {
		ID3D11Device* device = nvenc_info.get_device(nvenc_data_);
		ID3D11Texture2D* texture = nvenc_info.get_texture(nvenc_data_);
		ID3D11DeviceContext* context = nvenc_info.get_context(nvenc_data_);
		D3D11_MAPPED_SUBRESOURCE map;

		context->Map(texture, D3D11CalcSubresource(0, 0, 1), D3D11_MAP_WRITE, 0, &map);
		for (uint32_t y = 0; y < in_height; y++) {
			memcpy((uint8_t*)map.pData + y * map.RowPitch, &in_buffer[0] + y * in_width * 4, in_width * 4);
		}
		context->Unmap(texture, D3D11CalcSubresource(0, 0, 1));

		frame_size = nvenc_info.encode_texture(nvenc_data_, texture, out_buffer.get(),
			encoder_config_.video.width * encoder_config_.video.height * 4);
	}
	else if (qsv_encoder_.IsInitialized()) {
		frame_size = qsv_encoder_.Encode(in_buffer, in_width, in_height, out_buffer.get(), max_buffer_size);
	}
	else {	// FFmpeg软件编码（默认）

		/*
		作用：
			调用FFmpeg编码接口，将输入图像（in_buffer）编码为H.264数据包（AVPacket）。
		输入参数：
			in_buffer：原始图像数据。
			in_width/in_height：图像分辨率。
			image_size：输入数据大小。
		输出：
			pkt_ptr：智能指针包裹的AVPacket，存储编码后的H.264数据。
		*/
		ffmpeg::AVPacketPtr pkt_ptr = h264_encoder_.Encode(in_buffer, in_width, in_height, image_size);
		int extra_data_size = 0;
		uint8_t* extra_data = nullptr;

		// 处理关键帧与SPS/PPS
		if (pkt_ptr != nullptr) {		
			// 作用：检查当前数据包是否为关键帧（I帧）。
			
			if (IsKeyFrame(pkt_ptr->data, pkt_ptr->size)) {
				/* 编码器使用了AV_CODEC_FLAG_GLOBAL_HEADER, 这里需要添加sps, pps */

				/*
				背景：
					当编码器设置AV_CODEC_FLAG_GLOBAL_HEADER时，SPS（序列参数集）和PPS（图像参数集）存储在extradata中，而非每个关键帧内。
					RTP协议虽不强制要求，但在实际应用中，​建议在关键帧前携带SPS/PPS或周期性发送，以提升兼容性和容错性。这是工程实践中的常见优化，确保不同客户端和网络条件下的可靠解码。

					​场景：	​					是否需要关键帧前携带SPS/PPS：
​					SDP解析可靠且无中途加入		不需要
​					中途加入/兼容性要求高		需要
​					动态参数变更				需要（携带更新的SPS/PPS）

				操作：
					从编码器上下文中提取extradata（包含SPS/PPS）。
					将extradata拷贝到输出缓冲区的头部。
				*/

				/*
				AV_CODEC_FLAG_GLOBAL_HEADER 的作用：
					集中管理 SPS/PPS，减少冗余。
​				关键帧前插入 SPS/PPS 的原因：
					流媒体协议要求显式传递初始化参数。
					容错客户端中途加入或协议头丢失。
​				extradata 的存储位置：
					AVCodecContext->extradata，需手动插入到关键帧前。
				结论：
					在流媒体传输中，AV_CODEC_FLAG_GLOBAL_HEADER 和 ​关键帧前插入 SPS/PPS 是互补操作，前者用于集中管理参数，后者用于协议兼容性和容错性。
				*/

				/*
				场景：				AV_CODEC_FLAG_GLOBAL_HEADER 的作用：		关键帧前插入 SPS/PPS 的用途：
​				文件存储（MP4）​		SPS/PPS 写入文件头，不重复					无需插入
​				流传输（RTMP）​		SPS/PPS 存储在 extradata					需通过协议头发送，并在关键帧前插入
				*/

				/*
				关键概念解析
				(1) SPS/PPS
						SPS（Sequence Parameter Set）：定义视频分辨率、帧率、编码档次等全局参数。
						PPS（Picture Parameter Set）：定义熵编码模式、分片参数等图像级参数。
					存储位置：
						无AV_CODEC_FLAG_GLOBAL_HEADER：SPS/PPS嵌入每个关键帧。
						有AV_CODEC_FLAG_GLOBAL_HEADER：SPS/PPS存储在extradata，需手动添加。
				
				(2) AV_CODEC_FLAG_GLOBAL_HEADER
						作用：指示编码器将全局头信息（如SPS/PPS）分离到extradata中。
						典型场景：生成MP4文件时，SPS/PPS写入文件头；流媒体传输时需手动插入到关键帧前。
				*/

				extra_data = h264_encoder_.GetAVCodecContext()->extradata;
				extra_data_size = h264_encoder_.GetAVCodecContext()->extradata_size;
				memcpy(out_buffer.get(), extra_data, extra_data_size);
				frame_size += extra_data_size;
			}

			/*
			作用：
				将AVPacket中的H.264数据（如NALU单元）追加到输出缓冲区。
			结果：
				关键帧数据格式：[SPS][PPS][IDR帧数据]。
				非关键帧数据格式：[普通帧数据]。
			*/

			memcpy(out_buffer.get() + frame_size, pkt_ptr->data, pkt_ptr->size);
			frame_size += pkt_ptr->size;
		}
	}

	// 将临时缓冲区数据拷贝到out_frame，返回编码后数据大小。
	if (frame_size > 0) {
		out_frame.resize(frame_size);
		out_frame.assign(out_buffer.get(), out_buffer.get() + frame_size);
		return frame_size;
	}

	return 0;
}

int H264Encoder::GetSequenceParams(uint8_t* out_buffer, int out_buffer_size)
{
	int size = 0;

	if (!h264_encoder_.GetAVCodecContext()) {
		return -1;
	}

	if (nvenc_data_ != nullptr) {
		size = nvenc_info.get_sequence_params(nvenc_data_, (uint8_t*)out_buffer, out_buffer_size);
	}
	else if (qsv_encoder_.IsInitialized()) {
		size = qsv_encoder_.GetSequenceParams((uint8_t*)out_buffer, out_buffer_size);
	}
	else {
		AVCodecContext* codec_context = h264_encoder_.GetAVCodecContext();
		size = codec_context->extradata_size;
		memcpy(out_buffer, codec_context->extradata, codec_context->extradata_size);
	}

	return size;
}
