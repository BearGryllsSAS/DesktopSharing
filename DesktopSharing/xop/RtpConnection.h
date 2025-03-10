// PHZ
// 2018-6-8

#ifndef XOP_RTP_CONNECTION_H
#define XOP_RTP_CONNECTION_H

/*
RtpConnection 是多媒体传输系统的核心组件，负责：

管理 RTP/RTCP 通道的初始化
处理不同传输模式（TCP/UDP/组播）的细节
维护媒体流状态（播放、录制）
提供与 RTSP 协议的交互接口
其设计体现了网络协议栈的分层思想，通过抽象接口隐藏底层差异，适合作为流媒体服务器（如 IP Camera、视频会议系统）的基础模块。
*/

#include <cstdint>
#include <vector>
#include <string>
#include <memory>
#include <random>
#include "rtp.h"
#include "media.h"
#include "net/Socket.h"
#include "net/TcpConnection.h"

namespace xop
{

class RtspConnection;

class RtpConnection
{
public:
    RtpConnection(std::weak_ptr<TcpConnection> rtsp_connection);
    virtual ~RtpConnection();

    // 设置媒体通道的时钟频率。
    void SetClockRate(MediaChannelId channel_id, uint32_t clock_rate)
    { media_channel_info_[channel_id].clock_rate = clock_rate; }

    // 设置媒体通道的负载类型。
    void SetPayloadType(MediaChannelId channel_id, uint32_t payload)
    { media_channel_info_[channel_id].rtp_header.payload = payload; }

    // 初始化不同传输模式（TCP/UDP/组播）的 RTP 通道。
    bool SetupRtpOverTcp(MediaChannelId channel_id, uint16_t rtp_channel, uint16_t rtcp_channel);
    bool SetupRtpOverUdp(MediaChannelId channel_id, uint16_t rtp_port, uint16_t rtcp_port);
    bool SetupRtpOverMulticast(MediaChannelId channel_id, std::string ip, uint16_t port);

    uint32_t GetRtpSessionId() const
    { return (uint32_t)((size_t)(this)); }

    uint16_t GetRtpPort(MediaChannelId channel_id) const
    { return local_rtp_port_[channel_id]; }

    uint16_t GetRtcpPort(MediaChannelId channel_id) const
    { return local_rtcp_port_[channel_id]; }

    SOCKET GetRtpSocket(MediaChannelId channel_id) const
    { return rtpfd_[channel_id]; }

    SOCKET GetRtcpSocket(MediaChannelId channel_id) const
    { return rtcpfd_[channel_id]; }

    std::string GetIp()
    { return rtsp_ip_; }

    uint16_t GetPort()
    { return rtsp_port_; }
    
    bool IsMulticast() const
    { return is_multicast_; }

    bool IsSetup(MediaChannelId channel_id) const
    { return media_channel_info_[channel_id].is_setup; }

    std::string GetMulticastIp(MediaChannelId channel_id) const;

    void Play();        // 开始播放媒体流
    void Record();      // 开始录制媒体流
    void Teardown();    // 关闭连接，释放资源

    std::string GetRtpInfo(const std::string& rtsp_url);
    int SendRtpPacket(MediaChannelId channel_id, RtpPacket pkt);    // 发送 RTP 数据包。

    bool IsClosed() const
    { return is_closed_; }

    int GetId() const;

    bool HasKeyFrame() const
    { return has_key_frame_; }

private:
    friend class RtspConnection;
    friend class MediaSession;
    void SetFrameType(uint8_t frameType = 0);
    void SetRtpHeader(MediaChannelId channel_id, RtpPacket pkt);
    int  SendRtpOverTcp(MediaChannelId channel_id, RtpPacket pkt);
    int  SendRtpOverUdp(MediaChannelId channel_id, RtpPacket pkt);

    std::weak_ptr<TcpConnection> rtsp_connection_; // 关联的 RTSP 连接（弱引用）
    std::string rtsp_ip_;                          // RTSP 服务器 IP 地址
    uint16_t rtsp_port_;                           // RTSP 服务器端口号
    TransportMode transport_mode_;                 // 传输模式（TCP/UDP/组播）
    bool is_multicast_ = false;                    // 是否组播模式

    bool is_closed_ = false;                       // 连接是否已关闭
    bool has_key_frame_ = false;                   // 是否包含关键帧（用于视频流）
    uint8_t frame_type_ = 0;                       // 帧类型（如 I/P/B 帧）

    uint16_t local_rtp_port_[MAX_MEDIA_CHANNEL];   // 本地 RTP 端口数组（每个通道一个）
    uint16_t local_rtcp_port_[MAX_MEDIA_CHANNEL];  // 本地 RTCP 端口数组
    SOCKET rtpfd_[MAX_MEDIA_CHANNEL];              // RTP 套接字描述符数组
    SOCKET rtcpfd_[MAX_MEDIA_CHANNEL];             // RTCP 套接字描述符数组

    struct sockaddr_in peer_addr_;                              // 对端基础地址
    struct sockaddr_in peer_rtp_addr_[MAX_MEDIA_CHANNEL];       // 对端 RTP 地址
    struct sockaddr_in peer_rtcp_sddr_[MAX_MEDIA_CHANNEL];      // 对端 RTCP 地址
    MediaChannelInfo media_channel_info_[MAX_MEDIA_CHANNEL];    // 每个通道的配置和统计信息
};

}

#endif
