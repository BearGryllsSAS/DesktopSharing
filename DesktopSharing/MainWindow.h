#ifndef SCREEN_LIVE_MAIN_WINDOW_H
#define SCREEN_LIVE_MAIN_WINDOW_H

/*
MainWindow 类用于管理窗口和渲染的各项操作。
它包含了窗口创建、渲染、事件处理、视频更新等功能，并与 ScreenLive 类进行集成，负责视频的实时更新和渲染。
该类还包含了调试信息的显示、视频流的推送和处理等功能。
此外，它支持跨平台渲染，包括 SDL 和 Direct3D/OpenGL 渲染器的使用。
*/

#include "SDL.h"
#include "SDL_syswm.h"
#include "Overlay.h"
#include "ScreenLive.h"
#include <string>

class MainWindow : public OverlayCallack
{
public:
	MainWindow();
	virtual ~MainWindow();

	// 创建和销毁窗口
	bool Create();
	void Destroy();
	bool IsWindow() const;

	// 窗口尺寸调整
	void Resize();
	
	// 事件处理
	void Porcess(SDL_Event& event);

	// 调试信息
	void SetDebugInfo(std::string text);

	// 图像更新
	bool UpdateARGB(const uint8_t* data, uint32_t width, uint32_t height);
	
private:
	// 初始化和销毁渲染器和相关的图形上下文
	bool Init();
	void Clear();

	// 推流相关
	virtual bool StartLive(int& event_type, 
		std::vector<std::string>& encoder_settings,
		std::vector<std::string>& live_settings);

	virtual void StopLive(int event_type);

	// 窗口相关
	SDL_Window* window_   = nullptr;
	HWND window_handle_ = nullptr;

	Overlay* overlay_ = nullptr;
	std::string debug_info_text_;

	// 音视频相关参数
	AVConfig avconfig_;

	// 渲染器相关
	std::string renderer_name_;
	SDL_Renderer* renderer_   = nullptr;
	SDL_Texture*  texture_    = nullptr;

	IDirect3DDevice9* device_ = nullptr;
	SDL_GLContext gl_context_ = nullptr;

	// 纹理相关变量
	int texture_format_ = SDL_PIXELFORMAT_UNKNOWN;
	uint32_t texture_width_  = 0;
	uint32_t texture_height_ = 0;

	// 窗口和视频尺寸
	int window_width_   = 0;
	int window_height_  = 0;
	int video_width_    = 0;
	int video_height_   = 0;
	int overlay_width_  = 0;
	int overlay_height_ = 0;

	//  最小叠加层尺寸
	static const int kMinOverlayWidth  = 860;
	static const int kMinOverlayHeight = 200;
};

#endif