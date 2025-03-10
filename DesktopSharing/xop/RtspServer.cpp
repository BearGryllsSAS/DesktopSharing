#include "RtspServer.h"
#include "RtspConnection.h"
#include "net/SocketUtil.h"
#include "net/Logger.h"

using namespace xop;
using namespace std;

RtspServer::RtspServer(EventLoop* loop)
	: TcpServer(loop)
{

}

RtspServer::~RtspServer()
{
	
}

std::shared_ptr<RtspServer> RtspServer::Create(xop::EventLoop* loop)
{
	std::shared_ptr<RtspServer> server(new RtspServer(loop));
	return server;
}
/*
功能：添加一个媒体会话（如摄像头流），关联URL后缀（如 /live）和会话ID。
线程安全：通过 mutex_ 加锁，防止并发修改 rtsp_suffix_map_ 和 media_sessions_。
返回值：成功返回会话ID，冲突返回0（需调用者处理错误）。
*/
MediaSessionId RtspServer::AddSession(MediaSession* session)
{
    std::lock_guard<std::mutex> locker(mutex_);

    // 检查URL后缀是否已存在
    if (rtsp_suffix_map_.find(session->GetRtspUrlSuffix()) != rtsp_suffix_map_.end()) {
        return 0; // 冲突返回0
    }

    // 转换裸指针为智能指针，由RtspServer管理生命周期
    std::shared_ptr<MediaSession> media_session(session);
    MediaSessionId sessionId = media_session->GetMediaSessionId();

    // 插入映射关系
    rtsp_suffix_map_.emplace(media_session->GetRtspUrlSuffix(), sessionId);
    media_sessions_.emplace(sessionId, std::move(media_session));

    return sessionId;
}

/*
功能：通过会话ID移除会话，清理相关资源。
线程安全：操作前加锁，确保映射一致性。
*/
void RtspServer::RemoveSession(MediaSessionId sessionId)
{
    std::lock_guard<std::mutex> locker(mutex_);

    auto iter = media_sessions_.find(sessionId);
    if (iter != media_sessions_.end()) {
        // 删除URL后缀映射
        rtsp_suffix_map_.erase(iter->second->GetRtspUrlSuffix());
        // 删除会话
        media_sessions_.erase(sessionId);
    }
}

/*
会话查找
通过URL后缀查找
用途：客户端请求指定URL时（如 rtsp://server/live），通过后缀 /live 查找对应会话。
*/
MediaSession::Ptr RtspServer::LookMediaSession(const std::string& suffix)
{
    std::lock_guard<std::mutex> locker(mutex_);

    auto iter = rtsp_suffix_map_.find(suffix);
    if(iter != rtsp_suffix_map_.end()) {
        MediaSessionId id = iter->second;
        return media_sessions_[id];
    }

    return nullptr;
}

/*
会话查找
通过会话ID查找
用途：内部通过ID快速定位会话（如推送帧数据时）。
*/
MediaSession::Ptr RtspServer::LookMediaSession(MediaSessionId session_Id)
{
    std::lock_guard<std::mutex> locker(mutex_);

    auto iter = media_sessions_.find(session_Id);
    if(iter != media_sessions_.end()) {
        return iter->second;
    }

    return nullptr;
}

/*
数据推送 PushFrame
*/
bool RtspServer::PushFrame(MediaSessionId session_id, MediaChannelId channel_id, AVFrame frame)
{
    std::shared_ptr<MediaSession> sessionPtr = nullptr;

    // 加锁查找会话：快速锁定会话是否存在。
    {
        std::lock_guard<std::mutex> locker(mutex_);
        auto iter = media_sessions_.find(session_id);
        if (iter != media_sessions_.end()) {
            sessionPtr = iter->second;
        }
        else {
            return false;
        }
    }

    // 无锁处理帧数据：释放锁后调用 HandleFrame，避免阻塞其他操作。
    // 检查客户端数量：若无客户端订阅，直接返回失败。
    if (sessionPtr!=nullptr && sessionPtr->GetNumClient()!=0) {
        return sessionPtr->HandleFrame(channel_id, frame);
    }

    return false;
}

/*
客户端连接处理 OnConnect
功能：当新TCP连接到达时，创建 RtspConnection 对象处理RTSP协议。
*/
TcpConnection::Ptr RtspServer::OnConnect(SOCKET sockfd)
{	
	return std::make_shared<RtspConnection>(shared_from_this(), event_loop_->GetTaskScheduler().get(), sockfd);
}

