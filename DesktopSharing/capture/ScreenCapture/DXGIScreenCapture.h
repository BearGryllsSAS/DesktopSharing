// PHZ
// 2020-11-20

#ifndef DXGI_SCREEN_CAPTURE_H
#define DXGI_SCREEN_CAPTURE_H

/*
该代码通过DXGI输出复制接口实现高效的屏幕捕获，结合D3D11纹理和GDI光标绘制，提供了完整的屏幕捕获功能。其核心在于利用DXGI的桌面复制API和D3D11的硬件加速，适合需要高性能屏幕捕获的应用场景（如录屏、远程桌面）。
*/

#include "ScreenCapture.h"
#include "WindowHelper.h"
#include <cstdio>
#include <cstdint>
#include <string>
#include <mutex>
#include <thread>
#include <memory>
#include <vector>
#include <wrl.h>
#include <dxgi.h>
#include <d3d11.h>
#include <dxgi1_2.h>

/*
核心功能与流程
​	初始化：选择指定显示器，创建D3D11设备、DXGI输出复制接口，初始化共享纹理。
​	帧捕获：后台线程循环调用AcquireFrame获取最新帧，处理光标并复制到共享纹理。
​	数据提取：通过CaptureFrame将捕获的BGRA图像数据复制到用户提供的容器中。
​	资源管理：使用智能指针和COM接口自动管理资源，确保无泄漏。
*/
class DXGIScreenCapture : public ScreenCapture
{
public:
	DXGIScreenCapture();
	virtual ~DXGIScreenCapture();

	bool Init(int display_index = 0);
	bool Destroy();

	uint32_t GetWidth()  const { return dxgi_desc_.ModeDesc.Width; }
	uint32_t GetHeight() const { return dxgi_desc_.ModeDesc.Height; }

	bool CaptureFrame(std::vector<uint8_t>& bgra_image, uint32_t& width, uint32_t& height);
	//bool GetTextureHandle(HANDLE* handle, int* lockKey, int* unlockKey);
	//bool CaptureImage(std::string pathname);

	//ID3D11Device* GetD3D11Device() { return d3d11_device_.Get(); }
	//ID3D11DeviceContext* GetD3D11DeviceContext() { return d3d11_context_.Get(); }

	bool CaptureStarted() const
	{
		return is_started_;
	}

private:
	int StartCapture();
	int StopCapture();
	int CreateSharedTexture();
	int AquireFrame();

	// 存储目标显示器的位置和尺寸信息。
	DX::Monitor monitor_;

	// 标记初始化和捕获状态，防止重复操作。
	bool is_initialized_;
	bool is_started_;

	// 后台线程，持续调用AcquireFrame获取新帧。
	std::unique_ptr<std::thread> thread_ptr_;

	std::mutex mutex_;

	// 存储当前帧的BGRA像素数据及其大小。
	std::shared_ptr<uint8_t> image_ptr_; // bgra
	uint32_t image_size_;

	// d3d resource
	// 描述输出模式（如分辨率、格式），通过GetWidth/GetHeight暴露给用户。
	DXGI_OUTDUPL_DESC dxgi_desc_;

	// 共享纹理句柄和键控互斥锁，用于安全跨进程共享纹理。
	HANDLE texture_handle_;
	Microsoft::WRL::ComPtr<IDXGIKeyedMutex> keyed_mutex_;
	int key_;
	
	// D3D11设备和上下文，用于创建纹理和执行GPU命令。
	Microsoft::WRL::ComPtr<ID3D11Device> d3d11_device_;
	Microsoft::WRL::ComPtr<ID3D11DeviceContext> d3d11_context_;

	// DXGI输出复制接口，核心对象用于访问桌面图像。
	Microsoft::WRL::ComPtr<IDXGIOutputDuplication> dxgi_output_duplication_;

	// 纹理对象：shared_texture_用于跨进程共享；rgba_texture_为CPU可读的暂存纹理；gdi_texture_兼容GDI以绘制光标。
	Microsoft::WRL::ComPtr<ID3D11Texture2D> shared_texture_;
	Microsoft::WRL::ComPtr<ID3D11Texture2D> rgba_texture_;
	Microsoft::WRL::ComPtr<ID3D11Texture2D> gdi_texture_;

	
};

#endif