#ifndef __MEDIACTRL_HPP__
#define __MEDIACTRL_HPP__

#include <stdtype.h>
class MediaInfo;

class MediaControl
{
public:
	MediaControl();
	virtual ~MediaControl();
	virtual UINT8 Init(MediaInfo& mediaInfo);
	virtual void Deinit(void);
protected:
	static void SignalCB(MediaInfo* mInfo, void* userParam, UINT8 signalMask);
	virtual void SignalHandler(UINT8 signalMask);
};


#define MCTRLSIG_NONE	0x00
#define MCTRLSIG_WINKEY	0x10
#define MCTRLSIG_SMTC	0x11
#define MCTRLSIG_DBUS	0x20

MediaControl* GetMediaControl(UINT8 mcSig);


#endif	// __MEDIACTRL_HPP__
