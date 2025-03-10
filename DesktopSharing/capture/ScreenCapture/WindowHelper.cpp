#include "WindowHelper.h"

namespace DX {

std::vector<Monitor> GetMonitors()
{
	std::vector<Monitor> monitors;

	HRESULT hr = S_OK;

	// 1. ����Direct3D 9Ex����
	IDirect3D9Ex* d3d9ex = nullptr;
	hr = Direct3DCreate9Ex(D3D_SDK_VERSION, &d3d9ex);
	if (FAILED(hr)) {
		return monitors;	// ��ʼ��ʧ�ܷ��ؿ��б�
	}

	// 2. ��ȡ��������������ʾ��������
	int adapter_count = d3d9ex->GetAdapterCount();

	for (int i = 0; i < adapter_count; i++) {
		Monitor monitor;
		memset(&monitor, 0, sizeof(Monitor));

		// 3. ��ȡ������LUID������Ψһ��ʶ����
		LUID luid = { 0 , 0 };
		hr = d3d9ex->GetAdapterLUID(i, &luid);
		if (FAILED(hr)) {
			continue;	// ������Ч������
		}

		monitor.low_part = (uint64_t)luid.LowPart;		// ��32λ
		monitor.high_part = (uint64_t)luid.HighPart;	// ��32λ

		// 4. ��ȡ��������������ʾ�����
		HMONITOR hMonitor = d3d9ex->GetAdapterMonitor(i);
		if (hMonitor) {
			MONITORINFO monitor_info;
			monitor_info.cbSize = sizeof(MONITORINFO);
			// 5. ��ȡ��ʾ��������Ϣ
			BOOL ret = GetMonitorInfoA(hMonitor, &monitor_info);
			if (ret) {
				monitor.left = monitor_info.rcMonitor.left;
				monitor.right = monitor_info.rcMonitor.right;
				monitor.top = monitor_info.rcMonitor.top;
				monitor.bottom = monitor_info.rcMonitor.bottom;
				monitors.push_back(monitor);	// ��ӵ�����б�
			}
		}
	}

	d3d9ex->Release();	// 6. �ͷ�Direct3D����
	return monitors;
}

}
