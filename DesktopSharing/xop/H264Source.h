// PHZ
// 2018-5-16

#ifndef XOP_H264_SOURCE_H
#define XOP_H264_SOURCE_H

/*
ʵ����H.264��Ƶ���Ļ�����װ���ܣ�����ΪRTSP/RTP��ý��������ĺ���������������ʵ�����󲹳��Ƭ����Ͳ������ù��ܡ�
*/

#include "MediaSource.h"
#include "rtp.h"

namespace xop
{ 

// ���Ĺ��ܣ�H.264��Ƶ֡��װΪRTP�� + ����SDP������Ϣ
class H264Source : public MediaSource
{
public:
	// �������� CreateNew
	static H264Source* CreateNew(uint32_t framerate=25);
	~H264Source();

	void SetFramerate(uint32_t framerate)
	{ framerate_ = framerate; }

	uint32_t GetFramerate() const 
	{ return framerate_; }

	// SDP��ط���
	virtual std::string GetMediaDescription(uint16_t port); 
	virtual std::string GetAttribute(); 

	// ֡������ķ���
	bool HandleFrame(MediaChannelId channel_id, AVFrame frame);

	static uint32_t GetTimestamp();
	
private:
	H264Source(uint32_t framerate);

	// Ĭ��֡��
	uint32_t framerate_ = 25;
};
	
}

#endif



