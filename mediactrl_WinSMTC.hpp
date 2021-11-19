#ifndef __MEDIACTRL_SMTC_HPP__
#define __MEDIACTRL_SMTC_HPP__

#include <stdtype.h>
#include "mediactrl.hpp"

class MediaCtrlSMTC : public MediaControl
{
public:
	MediaCtrlSMTC();
	~MediaCtrlSMTC();
	UINT8 Init(MediaInfo& mediaInfo);
	void Deinit(void);
protected:
	void SignalHandler(UINT8 signalMask);
};

#endif	// __MEDIACTRL_SMTC_HPP__
