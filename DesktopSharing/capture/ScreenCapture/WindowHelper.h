#ifndef WINDOW_HELPER_H
#define WINDOW_HELPER_H

#include <vector>
#include <d3d9.h>

namespace DX {

struct Monitor
{
	uint64_t low_part;   // 显示器LUID的低32位
	uint64_t high_part;  // 显示器LUID的高32位

	int left;    // 显示器左边界坐标
	int top;     // 显示器上边界坐标
	int right;   // 显示器右边界坐标
	int bottom;  // 显示器下边界坐标
};

std::vector<Monitor> GetMonitors();	// 获取所有显示器信息

}

#endif