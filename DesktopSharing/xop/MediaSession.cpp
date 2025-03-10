// PHZ
// 2018-9-30

#include "MediaSession.h"
#include "RtpConnection.h"
#include <cstring>
#include <ctime>
#include <map>
#include <forward_list>
#include "net/Logger.h"
#include "net/SocketUtil.h"

using namespace xop;
using namespace std;

std::atomic_uint MediaSession::last_session_id_(1);

MediaSession::MediaSession(std::string url_suffxx)
	: suffix_(url_suffxx)
	, media_sources_(MAX_MEDIA_CHANNEL)  // 初始化媒体源容器（容量为MAX_MEDIA_CHANNEL）
	, buffer_(MAX_MEDIA_CHANNEL)         // 初始化环形缓冲区（每个通道独立）
{
	has_new_client_ = false;
	session_id_ = ++last_session_id_;    // 原子递增生成唯一会话ID

	for (int n = 0; n < MAX_MEDIA_CHANNEL; n++) {
		multicast_port_[n] = 0;          // 初始化组播端口为0
	}
}

/*
工厂方法创建会话实例，转移URL后缀所有权，避免拷贝。
*/
MediaSession* MediaSession::CreateNew(std::string url_suffix)
{
	return new MediaSession(std::move(url_suffix));
}

/*
若启用了组播，通过 MulticastAddr 单例释放分配的组播IP地址。
*/
MediaSession::~MediaSession()
{
	if (multicast_ip_ != "") {
		MulticastAddr::instance().Release(multicast_ip_);
	}
}

/*
注册客户端连接时的回调函数，用于日志或外部状态更新。
*/
void MediaSession::AddNotifyConnectedCallback(const NotifyConnectedCallback& callback)
{
	notify_connected_callbacks_.push_back(callback);
}

/*
注册客户端断开时的回调函数，用于日志或外部状态更新。
*/
void MediaSession::AddNotifyDisconnectedCallback(const NotifyDisconnectedCallback& callback)
{
	notify_disconnected_callbacks_.push_back(callback);
}

/*
添加媒体源并设置其发送回调，当媒体源产生RTP包时，遍历客户端发送数据。
*/
bool MediaSession::AddSource(MediaChannelId channel_id, MediaSource* source) {
	/*
	远程监控功能的 RTP 包就是在这里发送的
	*/
	source->SetSendFrameCallback([this](MediaChannelId channel_id, RtpPacket pkt) {
		// 遍历客户端并发送RTP包
		std::forward_list<std::shared_ptr<RtpConnection>> clients;
		std::map<int, RtpPacket> packets;
		{
			std::lock_guard<std::mutex> lock(map_mutex_);
			for (auto iter = clients_.begin(); iter != clients_.end();) {
				auto conn = iter->second.lock();
				if (conn == nullptr) {
					clients_.erase(iter++); // 清理无效连接
				}
				else {
					int id = conn->GetId();
					if (id >= 0) {
						if (packets.find(id) == packets.end()) {
							// 复制RTP包（避免修改原始数据）
							RtpPacket tmp_pkt;
							memcpy(tmp_pkt.data.get(), pkt.data.get(), pkt.size);
							tmp_pkt.size = pkt.size;
							tmp_pkt.last = pkt.last;
							tmp_pkt.timestamp = pkt.timestamp;
							tmp_pkt.type = pkt.type;
							packets.emplace(id, tmp_pkt);
						}
						clients.emplace_front(conn);
					}
					iter++;
				}
			}
		}

		// 发送RTP包给所有客户端
		int count = 0;
		for (auto iter : clients) {
			int ret = 0;
			int id = iter->GetId();
			if (id >= 0) {
				auto iter2 = packets.find(id);
				if (iter2 != packets.end()) {
					count++;
					ret = iter->SendRtpPacket(channel_id, iter2->second);
					if (is_multicast_ && ret == 0) {
						break; // 组播发送失败则终止
					}
				}
			}
		}
		return true;
		});

	media_sources_[channel_id].reset(source); // 把每一个 MediaSource* source 的发送回调函数设置好后，将 channel_id、source 的映射保存在成员变量中
	return true;
}

/*
移除指定通道的媒体源，释放资源。
*/
bool MediaSession::RemoveSource(MediaChannelId channel_id)
{
	media_sources_[channel_id] = nullptr;
	return true;
}

/*
启用组播，分配组播地址和端口。
*/
bool MediaSession::StartMulticast() {
	if (is_multicast_) return true;

	multicast_ip_ = MulticastAddr::instance().GetAddr(); // 获取组播IP
	if (multicast_ip_ == "") return false;

	std::random_device rd;
	multicast_port_[channel_0] = htons(rd() & 0xfffe); // 生成偶数端口
	multicast_port_[channel_1] = htons(rd() & 0xfffe);

	is_multicast_ = true;
	return true;
}

/*
生成SDP描述，包含会话信息、媒体格式及传输参数。
*/
std::string MediaSession::GetSdpMessage(std::string ip, std::string session_name) {
	if (sdp_ != "") return sdp_; // 返回缓存

	if (media_sources_.empty()) return "";

	char buf[2048] = { 0 };
	// 基础SDP字段
	snprintf(buf, sizeof(buf),
		"v=0\r\n"
		"o=- 9%ld 1 IN IP4 %s\r\n"
		"t=0 0\r\n"
		"a=control:*\r\n",
		(long)std::time(NULL), ip.c_str());

	if (session_name != "") {
		snprintf(buf + strlen(buf), sizeof(buf) - strlen(buf), "s=%s\r\n", session_name.c_str());
	}

	if (is_multicast_) {
		snprintf(buf + strlen(buf), sizeof(buf) - strlen(buf),
			"a=type:broadcast\r\n"
			"a=rtcp-unicast: reflection\r\n");
	}

	// 遍历媒体源，添加媒体描述
	for (uint32_t chn = 0; chn < media_sources_.size(); chn++) {
		if (media_sources_[chn]) {
			if (is_multicast_) {
				snprintf(buf + strlen(buf), sizeof(buf) - strlen(buf),
					"%s\r\nc=IN IP4 %s/255\r\n",
					media_sources_[chn]->GetMediaDescription(multicast_port_[chn]).c_str(),
					multicast_ip_.c_str());
			}
			else {
				snprintf(buf + strlen(buf), sizeof(buf) - strlen(buf),
					"%s\r\n",
					media_sources_[chn]->GetMediaDescription(0).c_str());
			}
			snprintf(buf + strlen(buf), sizeof(buf) - strlen(buf),
				"%s\r\na=control:track%d\r\n",
				media_sources_[chn]->GetAttribute().c_str(), chn);
		}
	}

	sdp_ = buf; // 缓存SDP
	return sdp_;
}

MediaSource* MediaSession::GetMediaSource(MediaChannelId channel_id)
{
	if (media_sources_[channel_id]) {
		return media_sources_[channel_id].get();
	}

	return nullptr;
}

/*
将音视频帧传递给对应通道的媒体源处理（如封装为RTP包）。
*/
bool MediaSession::HandleFrame(MediaChannelId channel_id, AVFrame frame)
{
	std::lock_guard<std::mutex> lock(mutex_);

	if(media_sources_[channel_id]) {
		media_sources_[channel_id]->HandleFrame(channel_id, frame);
	}
	else {
		return false;
	}

	return true;
}

/*
添加客户端连接，触发连接回调。
*/
bool MediaSession::AddClient(SOCKET rtspfd, std::shared_ptr<RtpConnection> rtp_conn)
{
	std::lock_guard<std::mutex> lock(map_mutex_);

	auto iter = clients_.find (rtspfd);
	if(iter == clients_.end()) {
		std::weak_ptr<RtpConnection> rtp_conn_weak_ptr = rtp_conn;
		clients_.emplace(rtspfd, rtp_conn_weak_ptr);
		for (auto& callback : notify_connected_callbacks_) {
			callback(session_id_, rtp_conn->GetIp(), rtp_conn->GetPort());
		}			
        
		has_new_client_ = true;
		return true;
	}
            
	return false;
}

/*
移除客户端连接，触发断开回调。
*/
void MediaSession::RemoveClient(SOCKET rtspfd)
{  
	std::lock_guard<std::mutex> lock(map_mutex_);

	auto iter = clients_.find(rtspfd);
	if (iter != clients_.end()) {
		auto conn = iter->second.lock();
		if (conn) {
			for (auto& callback : notify_disconnected_callbacks_) {
				callback(session_id_, conn->GetIp(), conn->GetPort());
			}				
		}
		clients_.erase(iter);
	}
}

