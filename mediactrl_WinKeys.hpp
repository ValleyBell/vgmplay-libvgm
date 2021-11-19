#ifndef __MEDIACTRL_WINKEYS_HPP__
#define __MEDIACTRL_WINKEYS_HPP__

#include <stdtype.h>
#include "mediactrl.hpp"

class MediaCtrlMKeys : public MediaControl
{
public:
	MediaCtrlMKeys();
	~MediaCtrlMKeys();
	UINT8 Init(MediaInfo& mediaInfo);
	void Deinit(void);
protected:
	void SignalHandler(UINT8 signalMask);
};

#endif	// __MEDIACTRL_WINKEYS_HPP__
