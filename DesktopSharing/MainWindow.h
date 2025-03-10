#ifndef SCREEN_LIVE_MAIN_WINDOW_H
#define SCREEN_LIVE_MAIN_WINDOW_H

/*
MainWindow �����ڹ����ں���Ⱦ�ĸ��������
�������˴��ڴ�������Ⱦ���¼�������Ƶ���µȹ��ܣ����� ScreenLive ����м��ɣ�������Ƶ��ʵʱ���º���Ⱦ��
���໹�����˵�����Ϣ����ʾ����Ƶ�������ͺʹ���ȹ��ܡ�
���⣬��֧�ֿ�ƽ̨��Ⱦ������ SDL �� Direct3D/OpenGL ��Ⱦ����ʹ�á�
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

	// ���������ٴ���
	bool Create();
	void Destroy();
	bool IsWindow() const;

	// ���ڳߴ����
	void Resize();
	
	// �¼�����
	void Porcess(SDL_Event& event);

	// ������Ϣ
	void SetDebugInfo(std::string text);

	// ͼ�����
	bool UpdateARGB(const uint8_t* data, uint32_t width, uint32_t height);
	
private:
	// ��ʼ����������Ⱦ������ص�ͼ��������
	bool Init();
	void Clear();

	// �������
	virtual bool StartLive(int& event_type, 
		std::vector<std::string>& encoder_settings,
		std::vector<std::string>& live_settings);

	virtual void StopLive(int event_type);

	// �������
	SDL_Window* window_   = nullptr;
	HWND window_handle_ = nullptr;

	Overlay* overlay_ = nullptr;
	std::string debug_info_text_;

	// ����Ƶ��ز���
	AVConfig avconfig_;

	// ��Ⱦ�����
	std::string renderer_name_;
	SDL_Renderer* renderer_   = nullptr;
	SDL_Texture*  texture_    = nullptr;

	IDirect3DDevice9* device_ = nullptr;
	SDL_GLContext gl_context_ = nullptr;

	// ������ر���
	int texture_format_ = SDL_PIXELFORMAT_UNKNOWN;
	uint32_t texture_width_  = 0;
	uint32_t texture_height_ = 0;

	// ���ں���Ƶ�ߴ�
	int window_width_   = 0;
	int window_height_  = 0;
	int video_width_    = 0;
	int video_height_   = 0;
	int overlay_width_  = 0;
	int overlay_height_ = 0;

	//  ��С���Ӳ�ߴ�
	static const int kMinOverlayWidth  = 860;
	static const int kMinOverlayHeight = 200;
};

#endif