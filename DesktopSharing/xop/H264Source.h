// PHZ
// 2018-5-16

#ifndef XOP_H264_SOURCE_H
#define XOP_H264_SOURCE_H

/*
实现了H.264视频流的基础封装功能，可作为RTSP/RTP流媒体服务器的核心组件，但需根据实际需求补充分片处理和参数配置功能。
*/

#include "MediaSource.h"
#include "rtp.h"

namespace xop
{ 

// 核心功能：H.264视频帧封装为RTP包 + 生成SDP描述信息
class H264Source : public MediaSource
{
public:
	// 工厂方法 CreateNew
	static H264Source* CreateNew(uint32_t framerate=25);
	~H264Source();

	void SetFramerate(uint32_t framerate)
	{ framerate_ = framerate; }

	uint32_t GetFramerate() const 
	{ return framerate_; }

	// SDP相关方法
	virtual std::string GetMediaDescription(uint16_t port); 
	virtual std::string GetAttribute(); 

	// 帧处理核心方法
	bool HandleFrame(MediaChannelId channel_id, AVFrame frame);

	static uint32_t GetTimestamp();
	
private:
	H264Source(uint32_t framerate);

	// 默认帧率
	uint32_t framerate_ = 25;
};
	
}

#endif



