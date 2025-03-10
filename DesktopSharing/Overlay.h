#ifndef OVERLAY_H
#define OVERLAY_H

/*
Overlay 类的核心功能是将额外的内容（如调试信息、状态信息或用户界面元素）绘制到屏幕上，而不干扰主图像或视频流的显示。
这个功能在视频流、游戏开发、实时监控等场景中非常有用，能提供实时的反馈信息和交互界面。
OverlayCallack 接口则允许其他类实现特定的行为或事件处理逻辑，以便动态控制叠加层的显示内容。
*/

#include "SDL.h"
#include "imgui/imgui.h"
#include "imgui/imgui_impl_dx9.h" 
#include "imgui/imgui_impl_sdl.h"
#include "imgui/imgui_impl_glfw.h"
#include "imgui/imgui_impl_opengl3.h"
#include <d3d9.h>
//#include <SDL_opengl.h>
#include <string>
#include <vector>

#include <GL/gl3w.h>            // Initialize with gl3wInit()
#include <GLFW/glfw3.h>

enum OverlayEventType
{
	EVENT_TYPE_RTSP_SERVER = 0x001,
	EVENT_TYPE_RTSP_PUSHER = 0x002,
	EVENT_TYPE_RTMP_PUSHER = 0x003,
};

/*
OverlayCallack 类是一个抽象基类，它定义了叠加层的回调接口。
其他类（如 MainWindow）可以继承并实现这些方法，以便在特定事件发生时，Overlay 类可以通过回调通知它们。
*/
class OverlayCallack
{
public:
	virtual bool StartLive(int& event_type, 
		std::vector<std::string>& encoder_settings,
		std::vector<std::string>& live_settings) = 0;

	virtual void StopLive(int event_type) = 0;

//protected:
	virtual ~OverlayCallack() {};
};

class Overlay
{
public:
	Overlay();
	virtual ~Overlay();

	// 注册观察者
	/*
	允许 Overlay 类注册一个回调接口（OverlayCallack）。
	这个接口通常由其他类（例如 MainWindow）实现，用于接收叠加层的事件通知，如直播状态变化。
	*/
	void RegisterObserver(OverlayCallack* callback);

	// 通过 IDirect3DDevice9 初始化，通常用于 Direct3D 渲染。		
	bool Init(SDL_Window* window, IDirect3DDevice9* device);
	// 通过 SDL_GLContext 初始化，适用于 OpenGL 渲染环境。
	bool Init(SDL_Window* window, SDL_GLContext gl_context);

	// 设置叠加层的矩形区域
	void SetRect(int x, int y, int w, int h);
	void Destroy();
	
	// 将叠加层渲染到屏幕上
	bool Render();

	// 处理来自 SDL 的事件
	static void Process(SDL_Event* event);

	// 设置推流的状态信息
	void SetLiveState(int event_type, bool state);

	// 设置显示在叠加层上的调试信息文本
	void SetDebugInfo(std::string text);

private:
	void Init();
	bool Copy();
	bool Begin();
	bool End();
	void NotifyEvent(int event_type);

	SDL_Window* window_ = nullptr;
	IDirect3DDevice9* device_ = nullptr;
	SDL_GLContext gl_context_ = nullptr;

	SDL_Rect rect_;

	OverlayCallack* callback_ = nullptr;

	/* debug info */
	std::string debug_info_text_;

	/* live config */
	int  encoder_index_ = 1;
	char encoder_bitrate_kbps_[8];
	char encoder_framerate_[3];

	struct LiveInfo {
		char server_ip[16];
		char server_port[6];
		char server_stream[16];
		char pusher_url[60];

		bool state = false;
		char state_info[16];
	} live_info_[10] ;
};

#endif