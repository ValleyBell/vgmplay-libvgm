#include <stdtype.h>
#include "mediactrl.hpp"

UINT8 MediaControl::Init(MediaInfo& mediaInfo)
{
	return 0x00;
}

void MediaControl::Deinit(void)
{
	return;
}

void MediaControl::ReadWriteDispatch(void)
{
	return;
}

/*static*/ void MediaControl::SignalCB(MediaInfo* mInfo, void* userParam, UINT8 signalMask)
{
	MediaControl* obj = static_cast<MediaControl*>(userParam);
	obj->SignalHandler(signalMask);
	return;
}

void MediaControl::SignalHandler(UINT8 signalMask)
{
	return;
}
