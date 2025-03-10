// PHZ
// 2018-6-8

#ifndef XOP_RTSP_H
#define XOP_RTSP_H

/*
Rtsp 类为RTSP协议处理提供基础框架，支持URL解析、认证配置和媒体会话查找。
其设计注重扩展性，通过虚函数允许派生类定制具体行为，适用于构建RTSP服务器或客户端库。
*/

#include <cstdio>
#include <string>
#include "MediaSession.h"
#include "net/Acceptor.h"
#include "net/EventLoop.h"
#include "net/Socket.h"
#include "net/Timer.h"

namespace xop
{

/*
存储解析后的RTSP URL信息，便于后续处理客户端请求时快速获取服务器地址和资源路径。
*/
struct RtspUrlInfo {
	std::string url;    // 完整的RTSP URL（如 "rtsp://192.168.1.100:554/live"）
	std::string ip;     // 服务器IP地址（如 "192.168.1.100"）
	uint16_t port;      // 服务器端口（如 554）
	std::string suffix; // URL路径后缀（如 "live"）
};

class Rtsp : public std::enable_shared_from_this<Rtsp>	// 继承 enable_shared_from_this 以支持安全传递 shared_ptr
{
public:
	Rtsp() : has_auth_info_(false) {}
	virtual ~Rtsp() {}

	// 设置HTTP Digest认证参数。
	virtual void SetAuthConfig(std::string realm, std::string username, std::string password)
	{
		realm_ = realm;
		username_ = username;
		password_ = password;
		has_auth_info_ = true;

		if (realm_=="" || username=="") {
			has_auth_info_ = false;
		}
	}

	// 设置/获取SDP中的会话名称（s= 字段），用于标识服务器版本或流名称。
	virtual void SetVersion(std::string version) // SDP Session Name
	{ version_ = std::move(version); }

	virtual std::string GetVersion()
	{ return version_; }

	// 返回完整的RTSP URL，用于日志或客户端交互。
	virtual std::string GetRtspUrl()
	{ return rtsp_url_info_.url; }

	// 解析RTSP URL，提取IP、端口和路径后缀。
	// url 格式需为 rtsp://{ip}:{port}/{suffix} 或 rtsp://{ip}/{suffix}。
	bool ParseRtspUrl(std::string url) {
		// 解析示例：rtsp://192.168.1.100:554/live
		char ip[100] = { 0 }, suffix[100] = { 0 };
		uint16_t port = 0;

		// 尝试解析带端口的URL（格式：rtsp://ip:port/suffix）
#if defined(__linux__) || defined(__linux)
		if (sscanf(url.c_str() + 7, "%[^:]:%hu/%s", ip, &port, suffix) == 3)
#elif defined(WIN32) || defined(_WIN32)
		if (sscanf_s(url.c_str() + 7, "%[^:]:%hu/%s", ip, 100, &port, suffix, 100) == 3)
#endif
		{
			rtsp_url_info_.port = port;
		}
		// 尝试解析默认端口（554）的URL（格式：rtsp://ip/suffix）
#if defined(__linux__) || defined(__linux)
		else if (sscanf(url.c_str() + 7, "%[^/]/%s", ip, suffix) == 2)
#elif defined(WIN32) || defined(_WIN32)
		else if (sscanf_s(url.c_str() + 7, "%[^/]/%s", ip, 100, suffix, 100) == 2)
#endif
		{
			rtsp_url_info_.port = 554;
		}
		else {
			return false; // 解析失败
		}

		rtsp_url_info_.ip = ip;
		rtsp_url_info_.suffix = suffix;
		rtsp_url_info_.url = url;
		return true;
	}

protected:
	friend class RtspConnection;

	// 媒体会话查找（需派生类实现）
	virtual MediaSession::Ptr LookMediaSession(const std::string& suffix)
	{ return nullptr; }

	virtual MediaSession::Ptr LookMediaSession(MediaSessionId sessionId)
	{ return nullptr; }

	bool has_auth_info_ = false;		// 标记是否启用认证（true 表示启用）。
	std::string realm_;					// 认证域（用于HTTP Digest认证）。
	std::string username_;				// 认证用户名。
	std::string password_;				// 认证密码。
	std::string version_;				// SDP中的会话名称（如 "Streaming Server"），通过 SetVersion 设置。
	struct RtspUrlInfo rtsp_url_info_;	// 存储解析后的RTSP URL信息（IP、端口、路径后缀等）。
};

}

#endif


