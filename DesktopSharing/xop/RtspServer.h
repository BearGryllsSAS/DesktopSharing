// PHZ
// 2020-4-2

#ifndef XOP_RTSP_SERVER_H
#define XOP_RTSP_SERVER_H

#include <memory>
#include <string>
#include <mutex>
#include <unordered_map>
#include "net/TcpServer.h"
#include "rtsp.h"

namespace xop
{

class RtspConnection;

/*
基于事件循环的RTSP流媒体服务器，支持多会话管理、客户端连接处理和实时音视频流推送。
通过组合TCP服务器和RTSP协议逻辑，实现了高效的媒体数据传输，适用于视频监控、直播等场景。
*/
/*
功能：
创建RTSP服务器，监听客户端连接。
管理多个媒体会话（MediaSession），每个会话对应一个媒体流（如摄像头视频）。
支持通过RTSP协议与客户端交互（如 DESCRIBE, SETUP, PLAY 命令）。
将音视频帧（AVFrame）推送到指定会话的客户端。
*/
class RtspServer : public Rtsp, public TcpServer
{
public:    
    // 工厂方法：创建 RtspServer 实例，依赖事件循环 EventLoop（通常基于Reactor模式处理I/O事件）。
	static std::shared_ptr<RtspServer> Create(xop::EventLoop* loop);
	~RtspServer();

    // 添加一个媒体会话，返回唯一ID。会话可能对应一个视频流或音频流。
    MediaSessionId AddSession(MediaSession* session);
    // 移除指定ID的会话，释放资源。
    void RemoveSession(MediaSessionId sessionId);

    // 将音视频帧（AVFrame）推送到指定会话的特定通道（如视频通道或音频通道）。
    // 内部可能通过RTP协议将帧数据发送给订阅该会话的客户端。
    bool PushFrame(MediaSessionId sessionId, MediaChannelId channelId, AVFrame frame);

private:
    friend class RtspConnection;

	RtspServer(xop::EventLoop* loop);

    // 根据URL后缀（如 /live）或会话ID查找媒体会话，用于处理客户端请求时定位资源。
    MediaSession::Ptr LookMediaSession(const std::string& suffix);
    MediaSession::Ptr LookMediaSession(MediaSessionId session_id);
    
    // 重写自 TcpServer，当新TCP连接到达时，创建 RtspConnection 对象处理RTSP协议逻辑。
    virtual TcpConnection::Ptr OnConnect(SOCKET sockfd);

    std::mutex mutex_;
    // 存储所有媒体会话，键为会话ID，值为会话对象的智能指针。
    std::unordered_map<MediaSessionId, std::shared_ptr<MediaSession>> media_sessions_;
    // 映射URL后缀到会话ID，例如将路径 /live 映射到ID 1，方便通过请求URL查找会话。
    std::unordered_map<std::string, MediaSessionId> rtsp_suffix_map_;
};

}

/*
设计亮点
资源管理：使用智能指针（std::shared_ptr）自动管理会话生命周期。
协议与传输分离：Rtsp 处理协议逻辑，TcpServer 处理网络传输，符合单一职责原则。
高性能：基于事件循环和非阻塞I/O，适合高并发场景。
*/

#endif

