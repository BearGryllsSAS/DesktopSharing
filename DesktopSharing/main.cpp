#include "ScreenLive.h"
#include "MainWindow.h"

#define ENABLE_SDL_WINDOW 1

#if ENABLE_SDL_WINDOW

#ifndef _DEBUG
#pragma comment(linker, "/subsystem:\"windows\" /entry:\"mainCRTStartup\"")
#endif

static const int SDL_USEREVENT_PAINT = 0x01;

/*
与定时器回调和事件处理相关的关键函数之一，主要作用是处理绘制操作，更新窗口的显示内容。
*/
static void OnPaint(void *param)
{
	// reinterpret_cast: 绕过类型系统的安全性检查的类型转换
	MainWindow* window = reinterpret_cast<MainWindow*>(param);

	if (window) {
		std::vector<uint8_t> bgra_image;
		uint32_t width = 0;
		uint32_t height = 0;
		// 获取屏幕的 RGBA 格式的图像数据、屏幕的宽度、屏幕的高度，并存储到相应的变量中
		if (ScreenLive::Instance().GetScreenImage(bgra_image, width, height)) {
			std::string status_info = ScreenLive::Instance().GetStatusInfo();
			window->SetDebugInfo(status_info);
			// 将获取到的 RGBA 格式的图像数据更新到窗口，刷新窗口的显示内容
			window->UpdateARGB(&bgra_image[0], width, height);
		}
	}
}

/*
实现了一个定时器回调函数 (TimerCallback)，它会在每隔一段时间后被触发，并向 SDL 事件队列中推送一个用户自定义事件（SDL_USEREVENT_PAINT）。
*/
static uint32_t TimerCallback(uint32_t interval, void *param)
{
	SDL_Event evt;
	memset(&evt, 0, sizeof(evt));
	evt.user.type = SDL_USEREVENT;
	evt.user.timestamp = 0;
	evt.user.code = SDL_USEREVENT_PAINT;
	evt.user.data1 = param;
	evt.user.data2 = nullptr;
	// 推送 evt 事件到 SDL 事件队列
	SDL_PushEvent(&evt);

	// 返回100表示定时器下次触发的事件为100毫秒 ---》 控制帧率
	return 10;
}

int main(int argc, char **argv)
{
	MainWindow window;
	SDL_TimerID timer_id = 0;

	// 创建一个 MainWindow 对象
	if (window.Create()) {
		// 启动 ScreenLive 的捕获功能
		if (ScreenLive::Instance().StartCapture() >= 0) {
			// 开启定时器，每隔一秒调用一次 TimerCallback 函数
			timer_id = SDL_AddTimer(1000, TimerCallback, &window);
		}		
		else {
			return -1;
		}
	}

	bool done = false;
	// 进入主时间循环，等待并处理 SDL 事件
	while (!done && window.IsWindow()) {
		SDL_Event event;
		if (SDL_WaitEvent(&event)) {
			window.Porcess(event);

			switch (event.type)
			{
				// 窗口大小发生变化
				case SDL_WINDOWEVENT: {
					if (event.window.event == SDL_WINDOWEVENT_SIZE_CHANGED) {
						window.Resize();
					}
					break;
				}

				// 自定义事件
				case SDL_USEREVENT: {
					if (event.user.code == SDL_USEREVENT_PAINT) {
						OnPaint(event.user.data1);
					}
					break;
				}

				// 退出窗口
				case SDL_QUIT: {
					done = true;
					break;
				}

				default: {
					break;
				}
			}
		}
	}
	
	// 清理资源
	if (timer_id) {
		SDL_RemoveTimer(timer_id);
	}

	window.Destroy();
	ScreenLive::Instance().Destroy();
	return 0;
}

#else 

#define RTSP_PUSHER_TEST "rtsp://127.0.0.1:554/test" 
#define RTMP_PUSHER_TEST "rtmp://127.0.0.1:1935/live/02"  

int main(int argc, char **argv)
{
	AVConfig avconfig;				// 配置视频流的参数
	avconfig.bitrate_bps = 4000000; // video bitrate
	avconfig.framerate = 25;        // video framerate
	avconfig.codec = "h264";		// hardware encoder: "h264_nvenc";        

	// 推流服务器的配置
	LiveConfig live_config;

	// server
	live_config.ip = "0.0.0.0";
	live_config.port = 8554;
	live_config.suffix = "live";

	// pusher
	live_config.rtmp_url = RTMP_PUSHER_TEST;
	live_config.rtsp_url = RTSP_PUSHER_TEST;

	// 初始化 ScreenLive 的捕获功能
	if (!ScreenLive::Instance().Init(avconfig)) {
		getchar();
		return 0;
	}

	// 启动 RTSP 推流服务器
	ScreenLive::Instance().StartLive(SCREEN_LIVE_RTSP_SERVER, live_config);
	//ScreenLive::Instance().StartLive(SCREEN_LIVE_RTSP_PUSHER, live_config);
	//ScreenLive::Instance().StartLive(SCREEN_LIVE_RTMP_PUSHER, live_config);

	while (1) {		
		//if (ScreenLive::Instance().IsConnected(SCREEN_LIVE_RTMP_PUSHER)) {

		//}

		//if (ScreenLive::Instance().IsConnected(SCREEN_LIVE_RTSP_PUSHER)) {

		//}

		std::this_thread::sleep_for(std::chrono::milliseconds(1000));
	}

	ScreenLive::Instance().StopLive(SCREEN_LIVE_RTSP_SERVER);
	//ScreenLive::Instance().StopLive(SCREEN_LIVE_RTSP_PUSHER);
	//ScreenLive::Instance().StopLive(SCREEN_LIVE_RTMP_PUSHER);

	ScreenLive::Instance().Destroy();

	getchar();
	return 0;
}

#endif