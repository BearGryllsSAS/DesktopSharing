#include "WindowHelper.h"

namespace DX {

std::vector<Monitor> GetMonitors()
{
	std::vector<Monitor> monitors;

	HRESULT hr = S_OK;

	// 1. 创建Direct3D 9Ex对象
	IDirect3D9Ex* d3d9ex = nullptr;
	hr = Direct3DCreate9Ex(D3D_SDK_VERSION, &d3d9ex);
	if (FAILED(hr)) {
		return monitors;	// 初始化失败返回空列表
	}

	// 2. 获取适配器数量（显示器数量）
	int adapter_count = d3d9ex->GetAdapterCount();

	for (int i = 0; i < adapter_count; i++) {
		Monitor monitor;
		memset(&monitor, 0, sizeof(Monitor));

		// 3. 获取适配器LUID（本地唯一标识符）
		LUID luid = { 0 , 0 };
		hr = d3d9ex->GetAdapterLUID(i, &luid);
		if (FAILED(hr)) {
			continue;	// 跳过无效适配器
		}

		monitor.low_part = (uint64_t)luid.LowPart;		// 低32位
		monitor.high_part = (uint64_t)luid.HighPart;	// 高32位

		// 4. 获取适配器关联的显示器句柄
		HMONITOR hMonitor = d3d9ex->GetAdapterMonitor(i);
		if (hMonitor) {
			MONITORINFO monitor_info;
			monitor_info.cbSize = sizeof(MONITORINFO);
			// 5. 获取显示器坐标信息
			BOOL ret = GetMonitorInfoA(hMonitor, &monitor_info);
			if (ret) {
				monitor.left = monitor_info.rcMonitor.left;
				monitor.right = monitor_info.rcMonitor.right;
				monitor.top = monitor_info.rcMonitor.top;
				monitor.bottom = monitor_info.rcMonitor.bottom;
				monitors.push_back(monitor);	// 添加到结果列表
			}
		}
	}

	d3d9ex->Release();	// 6. 释放Direct3D对象
	return monitors;
}

}
