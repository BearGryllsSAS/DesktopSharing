// PHZ
// 2018-6-11

#ifndef XOP_RTP_H
#define XOP_RTP_H

/*
定义了实时传输协议(RTP)相关的数据结构和枚举类型。
为RTP传输提供了基础框架，适合作为多媒体流传输系统的底层组件，实际使用时需要结合具体网络库实现IO操作，并注意字节序转换等网络编程细节。
*/

#include <memory>
#include <cstdint>

#define RTP_HEADER_SIZE        12      // RTP标准头部长度
#define MAX_RTP_PAYLOAD_SIZE   1420    // 有效载荷最大尺寸（考虑MTU避免分片）	1460->1500-20-12-8
#define RTP_VERSION           2        // RTP协议版本号
#define RTP_TCP_HEAD_SIZE     4        // TCP传输时的额外头部大小

namespace xop
{

// 传输模式枚举
enum TransportMode 
{
	RTP_OVER_TCP = 1,       // TCP传输
	RTP_OVER_UDP = 2,       // UDP传输
	RTP_OVER_MULTICAST = 3, // 组播传输
};

// RTP头部结构体
typedef struct _RTP_header 
{
	/* 小端序 */
	unsigned char csrc : 4;			// CSRC计数器
	unsigned char extension : 1;	// 扩展标志
	unsigned char padding : 1;		// 填充标志
	unsigned char version : 2;		// 协议版本（固定为2）

	unsigned char payload : 7;		// 负载类型
	unsigned char marker : 1;		// 标记位

	unsigned short seq;				// 序列号（16位）
	unsigned int   ts;				// 时间戳（32位）
	unsigned int   ssrc;			// 同步源标识符（32位）
} RtpHeader;

// 媒体通道信息结构体
struct MediaChannelInfo 
{
	// RTP基础头
	RtpHeader rtp_header;

	// TCP相关
	uint16_t rtp_channel;       // RTP子通道号（TCP交互时使用）
	uint16_t rtcp_channel;      // RTCP子通道号

	// UDP相关
	uint16_t rtp_port;          // RTP端口号
	uint16_t rtcp_port;         // RTCP端口号
	uint16_t packet_seq;        // 包序列号
	uint32_t clock_rate;        // 时钟频率（Hz）

	// RTCP统计
	uint64_t packet_count;      // 发送/接收的包总数
	uint64_t octet_count;       // 发送/接收的字节总数
	uint64_t last_rtcp_ntp_time;// 最后一次RTCP时间戳

	// 状态标志
	bool is_setup;              // 通道是否已建立
	bool is_play;               // 是否处于播放状态
	bool is_record;             // 是否处于录制状态
};

// RTP数据包结构体
struct RtpPacket 
{
	RtpPacket() : data(new uint8_t[1600], std::default_delete<uint8_t[]>()) {
		type = 0;  // 初始化类型
	}

	std::shared_ptr<uint8_t> data;  // 数据缓冲区（智能指针管理）
	uint32_t size;                  // 数据实际大小
	uint32_t timestamp;             // 时间戳
	uint8_t  type;                  // 数据类型（可能用于区分音视频）
	uint8_t  last;                  // 是否最后一个分片
};

}

#endif
