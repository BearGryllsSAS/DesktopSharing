#ifndef WINDOW_HELPER_H
#define WINDOW_HELPER_H

#include <vector>
#include <d3d9.h>

namespace DX {

struct Monitor
{
	uint64_t low_part;   // ��ʾ��LUID�ĵ�32λ
	uint64_t high_part;  // ��ʾ��LUID�ĸ�32λ

	int left;    // ��ʾ����߽�����
	int top;     // ��ʾ���ϱ߽�����
	int right;   // ��ʾ���ұ߽�����
	int bottom;  // ��ʾ���±߽�����
};

std::vector<Monitor> GetMonitors();	// ��ȡ������ʾ����Ϣ

}

#endif