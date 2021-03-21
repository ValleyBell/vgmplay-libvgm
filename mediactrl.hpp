#ifndef __MEDIACTRL_HPP__
#define __MEDIACTRL_HPP__

#include <stdtype.h>
class MediaInfo;

class MediaControl
{
public:
	UINT8 Init(MediaInfo& mediaInfo);
	void Deinit(void);
private:
	static void SignalCB(MediaInfo* mInfo, void* userParam, UINT8 signalMask);
	void SignalHandler(UINT8 signalMask);
};

#endif	// __MEDIACTRL_HPP__
