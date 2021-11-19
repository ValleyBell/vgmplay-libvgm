#include <stddef.h>
#include <stdtype.h>
#include "mediactrl.hpp"

#ifdef MEDIACTRL_WINKEYS
#include "mediactrl_WinKeys.hpp"
#endif
#ifdef MEDIACTRL_SMTC
#include "mediactrl_WinSMTC.hpp"
#endif
#ifdef MEDIACTRL_DBUS
#include "mediactrl_dbus.hpp"
#endif

MediaControl::MediaControl()
{
}

MediaControl::~MediaControl()
{
	Deinit();
}

UINT8 MediaControl::Init(MediaInfo& mediaInfo)
{
	return 0x00;
}

void MediaControl::Deinit(void)
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


MediaControl* GetMediaControl(UINT8 mcSig)
{
	switch(mcSig)
	{
	case MCTRLSIG_NONE:
		return new MediaControl;
#ifdef MEDIACTRL_WINKEYS
	case MCTRLSIG_WINKEY:
		return new MediaCtrlMKeys;
#endif
#ifdef MEDIACTRL_SMTC
	case MCTRLSIG_SMTC:
		return new MediaCtrlSMTC;
#endif
#ifdef MEDIACTRL_DBUS
	case MCTRLSIG_DBUS:
		return new MediaCtrlDBus;
#endif
	default:
		return NULL;
	}
}
