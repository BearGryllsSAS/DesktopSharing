// PHZ
// 2018-5-16

#if defined(WIN32) || defined(_WIN32)
#ifndef _CRT_SECURE_NO_WARNINGS
#define _CRT_SECURE_NO_WARNINGS
#endif
#endif

#include "H264Source.h"
#include <cstdio>
#include <chrono>
#if defined(__linux) || defined(__linux__)
#include <sys/time.h>
#endif

using namespace xop;
using namespace std;

H264Source::H264Source(uint32_t framerate)
	: framerate_(framerate)
{
    payload_    = 96;       // RFC 3551                 动态RTP负载类型标识
    media_type_ = H264;     // RFC 6184	                媒体类型标识
    clock_rate_ = 90000;    // RFC 6184 Section 4.2     90kHz时钟基准（H.264标准要求）
}

H264Source* H264Source::CreateNew(uint32_t framerate)
{
    return new H264Source(framerate);
}

H264Source::~H264Source()
{

}

string H264Source::GetMediaDescription(uint16_t port)
{
    char buf[100] = {0};
    sprintf(buf, "m=video %hu RTP/AVP 96", port); // \r\nb=AS:2000
    return string(buf);
}

string H264Source::GetAttribute()
{
    return string("a=rtpmap:96 H264/90000");
}

/*
这段代码是 H264Source 类的 HandleFrame 方法，其核心功能是将 H.264 视频帧封装为 RTP 数据包并通过回调函数发送。代码根据 H.264 数据的大小，分为两种处理方式：

​单包模式：数据较小（不超过 RTP 负载最大限制），直接封装为一个 RTP 包。
​分片模式（FU-A）​：数据较大时，分割为多个 RTP 包发送。
*/
/*
代码功能：
    将 H.264 帧按 RFC 6184 规范封装为 RTP 包，支持单包和 FU-A 分片模式。
​符合性：
    基本遵循 FU-A 分片规则，但需验证扩展头的必要性。
​优化建议：
    明确扩展头用途或移除。
    增加对 SPS/PPS 的特殊处理（如标记关键帧）。
    校验输入数据是否已去除起始码。
*/
/*
关键细节与规范验证
​   1. FU-A 分片头构造
​       起始分片：S=1, E=0。
​       中间分片：S=0, E=0。
​       结束分片：S=0, E=1。
​       NALU 类型保留：通过 FU_A[1] 低 5 位传递原 NALU 类型（如 I 帧为 5）。
​   2. RTP 包格式
​       RTP 头部：固定 12 字节（未显式填充，可能由外部处理）。
​       扩展头：4 字节自定义字段（用途未明确，可能用于标记帧类型）。
​       负载：FU-A 头（2 字节） + H.264 数据。
​   3. 时间戳同步
​       所有分片：共用同一时间戳，确保接收端按正确顺序重组。
​   4. 分片大小计算
​   单包最大负载：MAX_RTP_PAYLOAD_SIZE = 1400（假设 MTU=1500，扣除 IP/UDP/RTP 头）。
​   负载分配：
    中间分片：MAX_RTP_PAYLOAD_SIZE-2 字节。
    最后分片：剩余字节数。
​   
潜在问题与改进
​   1. 扩展头的用途
       代码中为 RTP 包添加了 4 字节扩展头，但未说明其作用。需确认：
       
       是否符合 RFC 规范？若为私有扩展，需在 SDP 中声明。
       是否干扰接收端解析？例如，某些播放器可能不识别扩展头。
​   2. 起始码处理
​       输入数据假设：frame_buf 应已去除 H.264 起始码（如 00 00 00 01），否则分片逻辑会包含无效数据。
​       验证点：确认 PushVideo 函数是否已正确去除起始码。
​   3. 分片大小计算的边界条件
​       最后分片大小：当 frame_size 极小时（如 1 字节），是否可能溢出？
       示例：frame_size=1，则 4 + RTP_HEADER_SIZE + 2 + 1 是合法的。
​   4. NAL 单元类型处理
​       关键 NALU：SPS/PPS 通常较小，应通过单包模式发送。
​       分片限制：分片仅适用于类型为 1（非 IDR）、5（IDR）等的 NALU。
*/
bool H264Source::HandleFrame(MediaChannelId channel_id, AVFrame frame)
{
    uint8_t* frame_buf  = frame.buffer.get();
    uint32_t frame_size = frame.size;

    if (frame.timestamp == 0) {
	    frame.timestamp = GetTimestamp();
    }    

    if (frame_size <= MAX_RTP_PAYLOAD_SIZE) {   // 单包模式处理

        /*
        适用场景：
            H.264 NALU（如 SPS/PPS 或小帧）可直接放入单个 RTP 包。
​        关键操作：
​           RTP 头扩展：添加 4 字节的自定义扩展头（可能用于标记帧类型）。
​           时间戳同步：所有分片共享同一时间戳，确保接收端同步重组。
​           负载拷贝：将 H.264 数据直接拷贝到 RTP 负载区。
        */

        RtpPacket rtp_pkt;
	    rtp_pkt.type = frame.type;
	    rtp_pkt.timestamp = frame.timestamp;
	    rtp_pkt.size = frame_size + 4 + RTP_HEADER_SIZE;    // 4字节扩展头 + RTP头
	    rtp_pkt.last = 1;                                   // 标记为最后一个包
        memcpy(rtp_pkt.data.get()+4+RTP_HEADER_SIZE, frame_buf, frame_size); 

        // 调用回调函数发送
        if (send_frame_callback_) {
		    if (!send_frame_callback_(channel_id, rtp_pkt)) {
			    return false;
		    }               
        }
    }
    else {  // 分片模式处理（FU-A）​ 当 H.264 数据超过 RTP 负载限制时，使用 ​RFC 6184 定义的 FU-A 分片机制。

        char FU_A[2] = { 0 };
        FU_A[0] = (frame_buf[0] & 0xE0) | 28;   // 高 3 位保留，类型设为 28（FU-A）
        FU_A[1] = 0x80 | (frame_buf[0] & 0x1f); // 起始分片（S=1）

        frame_buf += 1;     // 跳过原 NALU 头（已包含在 FU-A 头中）
        frame_size -= 1;

        // 分片循环处理
        /*
        分片逻辑：
​        中间分片：S=0, E=0，负载大小为 MAX_RTP_PAYLOAD_SIZE-2（扣除 FU-A 头）。
​        指针调整：每次处理 MAX_RTP_PAYLOAD_SIZE-2 字节，更新剩余数据指针和大小。
        */
        while (frame_size + 2 > MAX_RTP_PAYLOAD_SIZE) {
            // 构造中间分片
            RtpPacket rtp_pkt;
            rtp_pkt.type = frame.type;
            rtp_pkt.timestamp = frame.timestamp;
            rtp_pkt.size = 4 + RTP_HEADER_SIZE + MAX_RTP_PAYLOAD_SIZE;
            rtp_pkt.last = 0; // 非最后一个包

            // 设置 FU-A 头（S=0, E=0）
            rtp_pkt.data.get()[RTP_HEADER_SIZE + 4] = FU_A[0];
            rtp_pkt.data.get()[RTP_HEADER_SIZE + 5] = FU_A[1];
            // 拷贝分片数据
            memcpy(rtp_pkt.data.get() + 4 + RTP_HEADER_SIZE + 2, frame_buf, MAX_RTP_PAYLOAD_SIZE - 2);

            // 发送
            if (send_frame_callback_ && !send_frame_callback_(channel_id, rtp_pkt)) return false;

            // 调整指针和剩余大小
            frame_buf += MAX_RTP_PAYLOAD_SIZE - 2;
            frame_size -= MAX_RTP_PAYLOAD_SIZE - 2;

            FU_A[1] &= ~0x80; // 清除起始位（后续分片 S=0）
        }

        // 处理最后一个分片
        // 结束分片：E=1，剩余数据全部拷贝到负载中。
        {
            RtpPacket rtp_pkt;
            rtp_pkt.type = frame.type;
            rtp_pkt.timestamp = frame.timestamp;
            rtp_pkt.size = 4 + RTP_HEADER_SIZE + 2 + frame_size; // 最后分片大小
            rtp_pkt.last = 1;                                    // 标记为最后一个包

            FU_A[1] |= 0x40; // 设置结束位（E=1）
            rtp_pkt.data.get()[RTP_HEADER_SIZE + 4] = FU_A[0];
            rtp_pkt.data.get()[RTP_HEADER_SIZE + 5] = FU_A[1];
            memcpy(rtp_pkt.data.get() + 4 + RTP_HEADER_SIZE + 2, frame_buf, frame_size);

            if (send_frame_callback_ && !send_frame_callback_(channel_id, rtp_pkt)) return false;
        }
    }

    return true;
}

/*
时间戳生成实现
*/
uint32_t H264Source::GetTimestamp()
{
/* #if defined(__linux) || defined(__linux__)
    struct timeval tv = {0};
    gettimeofday(&tv, NULL);
    uint32_t ts = ((tv.tv_sec*1000)+((tv.tv_usec+500)/1000))*90; // 90: _clockRate/1000;
    return ts;
#else  */
    auto time_point = chrono::time_point_cast<chrono::microseconds>(chrono::steady_clock::now());
    return (uint32_t)((time_point.time_since_epoch().count() + 500) / 1000 * 90 );
//#endif
}
 