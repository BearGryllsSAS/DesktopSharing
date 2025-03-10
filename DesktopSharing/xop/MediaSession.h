// PHZ
// 2018-6-8

#ifndef XOP_MEDIA_SESSION_H
#define XOP_MEDIA_SESSION_H

/*
MediaSession 类是RTSP流媒体服务器的核心模块，负责管理媒体流、客户端连接及数据传输。
每个 MediaSession 会话对应一个唯一的媒体流（如摄像头视频），可通过RTSP协议被多个客户端订阅。
支持单播和组播模式，适用于实时音视频传输场景。
通过结合 RtspServer 和 RtpConnection，构建了一个完整的流媒体服务架构。
*/

#include <memory>
#include <string>
#include <vector>
#include <mutex>
#include <atomic>
#include <random>
#include <cstdint>
#include <unordered_set>
#include "media.h"
#include "H264Source.h"
#include "H265Source.h"
#include "G711ASource.h"
#include "AACSource.h"
#include "MediaSource.h"
#include "net/Socket.h"
#include "net/RingBuffer.h"

namespace xop
{

class RtpConnection;

class MediaSession
{
public:
	using Ptr = std::shared_ptr<MediaSession>;
	using NotifyConnectedCallback = std::function<void (MediaSessionId sessionId, std::string peer_ip, uint16_t peer_port)> ;
	using NotifyDisconnectedCallback = std::function<void (MediaSessionId sessionId, std::string peer_ip, uint16_t peer_port)> ;

	// 工厂方法，创建会话实例，初始化URL后缀，分配唯一 session_id_（通过 last_session_id_ 原子递增）。
	static MediaSession* CreateNew(std::string url_suffix="live");
	// 析构时释放所有媒体源和客户端资源，若启用组播，释放组播地址（通过 MulticastAddr 单例）。
	virtual ~MediaSession();


	// 添加媒体源（如H.264视频、AAC音频）到指定通道（CHANNEL_VIDEO/CHANNEL_AUDIO）。
	bool AddSource(MediaChannelId channel_id, MediaSource* source);
	// 功能：移除指定通道的媒体源。
	bool RemoveSource(MediaChannelId channel_id);
	// 返回通道对应的媒体源指针，供外部访问编码参数或发送逻辑。
	MediaSource* GetMediaSource(MediaChannelId channel_id);

	// 启用组播，通过 MulticastAddr 单例分配组播地址
	bool StartMulticast();
	// 返回是否启用组播。
	bool IsMulticast() const
	{
		return is_multicast_;
	}
	// 获取组播地址和端口，供客户端 SETUP 请求响应。
	std::string GetMulticastIp() const
	{
		return multicast_ip_;
	}
	uint16_t GetMulticastPort(MediaChannelId channel_id) const
	{
		if (channel_id >= MAX_MEDIA_CHANNEL) {
			return 0;
		}
		return multicast_port_[channel_id];
	}

	// 注册回调函数，当客户端连接或断开时触发，用于日志记录或外部状态更新。
	void AddNotifyConnectedCallback(const NotifyConnectedCallback& callback);
	void AddNotifyDisconnectedCallback(const NotifyDisconnectedCallback& callback);

	std::string GetRtspUrlSuffix() const
	{ return suffix_; }

	void SetRtspUrlSuffix(std::string& suffix)
	{ suffix_ = suffix; }

	// 生成SDP描述，包含媒体格式、传输协议、组播/单播地址等信息。
	std::string GetSdpMessage(std::string ip, std::string session_name ="");

	bool HandleFrame(MediaChannelId channel_id, AVFrame frame);

	// 将客户端Socket与RTP连接关联
	bool AddClient(SOCKET rtspfd, std::shared_ptr<RtpConnection> rtp_conn);
	// 移除客户端连接
	void RemoveClient(SOCKET rtspfd);
	// 返回当前连接的客户端数量，用于统计或资源控制。
	uint32_t GetNumClient() const
	{
		return (uint32_t)clients_.size();
	}

	MediaSessionId GetMediaSessionId()
	{ return session_id_; }

private:
	friend class MediaSource;
	friend class RtspServer;
	MediaSession(std::string url_suffxx);

	MediaSessionId session_id_ = 0;
	std::string suffix_;
	std::string sdp_;

	std::vector<std::unique_ptr<MediaSource>> media_sources_;
	std::vector<RingBuffer<AVFrame>> buffer_;

	std::vector<NotifyConnectedCallback> notify_connected_callbacks_;
	std::vector<NotifyDisconnectedCallback> notify_disconnected_callbacks_;
	std::mutex mutex_;
	std::mutex map_mutex_;
	std::map<SOCKET, std::weak_ptr<RtpConnection>> clients_;

	bool is_multicast_ = false;
	uint16_t multicast_port_[MAX_MEDIA_CHANNEL];
	std::string multicast_ip_;
	std::atomic_bool has_new_client_;

	static std::atomic_uint last_session_id_;
};

class MulticastAddr
{
public:
	static MulticastAddr& instance()
	{
		static MulticastAddr s_multi_addr;
		return s_multi_addr;
	}

	std::string GetAddr()
	{
		std::lock_guard<std::mutex> lock(mutex_);
		std::string addr_str;
		struct sockaddr_in addr = { 0 };
		std::random_device rd;

		for (int n = 0; n <= 10; n++) {
			uint32_t range = 0xE8FFFFFF - 0xE8000100;
			addr.sin_addr.s_addr = htonl(0xE8000100 + (rd()) % range);
			addr_str = inet_ntoa(addr.sin_addr);

			if (addrs_.find(addr_str) != addrs_.end()) {
				addr_str.clear();
			}
			else {
				addrs_.insert(addr_str);
				break;
			}
		}

		return addr_str;
	}

	void Release(std::string addr) {
		std::lock_guard<std::mutex> lock(mutex_);
		addrs_.erase(addr);
	}

private:
	std::mutex mutex_;
	std::unordered_set<std::string> addrs_;
};

}

#endif
