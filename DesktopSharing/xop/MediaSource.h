// PHZ
// 2018-6-8

#ifndef XOP_MEDIA_SOURCE_H
#define XOP_MEDIA_SOURCE_H

/*
MediaSource 的抽象基类，用于处理多媒体数据（如音视频）的封装和传输，通常用于实时流媒体传输（如 RTP/RTCP）。

MediaSource 类是一个高度抽象的媒体处理基类，通过定义统一的接口和回调机制，
实现了多媒体数据的处理、封装与网络传输的解耦，为构建灵活的多媒体传输系统提供了基础框架。
*/

#include "media.h"
#include "rtp.h"
#include "net/Socket.h"
#include <string>
#include <memory>
#include <cstdint>
#include <functional>
#include <map>

namespace xop
{

/*
功能：
	提供多媒体数据的抽象接口，支持不同媒体类型（如音频、视频）的帧处理、RTP 包封装和网络传输。
设计目标：
	通用性：通过继承实现不同媒体类型（如 H.264、AAC）的具体处理逻辑。
	解耦：媒体数据处理与网络传输分离，通过回调函数处理 RTP 包的发送。
	SDP 兼容：生成符合 SDP（Session Description Protocol）协议的媒体描述和属性，用于会话协商。
*/
class MediaSource
{
public:
	using SendFrameCallback = std::function<bool (MediaChannelId channel_id, RtpPacket pkt)>;

	MediaSource() {}
	virtual ~MediaSource() {}

	virtual MediaType GetMediaType() const
	{ return media_type_; }

	// 生成 SDP 中的媒体描述行（如 m=video 0 RTP/AVP 96）。
	virtual std::string GetMediaDescription(uint16_t port=0) = 0;

	// 生成 SDP 属性行（如 a=rtpmap:96 H264/90000）。
	virtual std::string GetAttribute()  = 0;

	// 核心方法：处理输入的 AVFrame 数据（如视频帧或音频帧），将其封装为 RTP 包。
	virtual bool HandleFrame(MediaChannelId channelId, AVFrame frame) = 0;
	// 定义发送 RTP 包的回调函数，由外部（如网络模块）实现具体发送逻辑（如通过 Socket）。
	virtual void SetSendFrameCallback(const SendFrameCallback callback)
	{ send_frame_callback_ = callback; }

	virtual uint32_t GetPayloadType() const
	{ return payload_; }

	virtual uint32_t GetClockRate() const
	{ return clock_rate_; }

protected:
	MediaType media_type_ = NONE;
	uint32_t  payload_    = 0;
	uint32_t  clock_rate_ = 0;
	SendFrameCallback send_frame_callback_;
};

}

#endif
