#ifndef OVERLAY_H
#define OVERLAY_H

/*
Overlay ��ĺ��Ĺ����ǽ���������ݣ��������Ϣ��״̬��Ϣ���û�����Ԫ�أ����Ƶ���Ļ�ϣ�����������ͼ�����Ƶ������ʾ��
�����������Ƶ������Ϸ������ʵʱ��صȳ����зǳ����ã����ṩʵʱ�ķ�����Ϣ�ͽ������档
OverlayCallack �ӿ�������������ʵ���ض�����Ϊ���¼������߼����Ա㶯̬���Ƶ��Ӳ����ʾ���ݡ�
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
OverlayCallack ����һ��������࣬�������˵��Ӳ�Ļص��ӿڡ�
�����ࣨ�� MainWindow�����Լ̳в�ʵ����Щ�������Ա����ض��¼�����ʱ��Overlay �����ͨ���ص�֪ͨ���ǡ�
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

	// ע��۲���
	/*
	���� Overlay ��ע��һ���ص��ӿڣ�OverlayCallack����
	����ӿ�ͨ���������ࣨ���� MainWindow��ʵ�֣����ڽ��յ��Ӳ���¼�֪ͨ����ֱ��״̬�仯��
	*/
	void RegisterObserver(OverlayCallack* callback);

	// ͨ�� IDirect3DDevice9 ��ʼ����ͨ������ Direct3D ��Ⱦ��		
	bool Init(SDL_Window* window, IDirect3DDevice9* device);
	// ͨ�� SDL_GLContext ��ʼ���������� OpenGL ��Ⱦ������
	bool Init(SDL_Window* window, SDL_GLContext gl_context);

	// ���õ��Ӳ�ľ�������
	void SetRect(int x, int y, int w, int h);
	void Destroy();
	
	// �����Ӳ���Ⱦ����Ļ��
	bool Render();

	// �������� SDL ���¼�
	static void Process(SDL_Event* event);

	// ����������״̬��Ϣ
	void SetLiveState(int event_type, bool state);

	// ������ʾ�ڵ��Ӳ��ϵĵ�����Ϣ�ı�
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