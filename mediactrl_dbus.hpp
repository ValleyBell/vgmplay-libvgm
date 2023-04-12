#ifndef __MEDIACTRL_DBUS_HPP__
#define __MEDIACTRL_DBUS_HPP__

#include <string>
#include <stdtype.h>
#include "mediactrl.hpp"

class MediaCtrlDBus : public MediaControl
{
public:
	MediaCtrlDBus();
	~MediaCtrlDBus();
	UINT8 Init(MediaInfo& mediaInfo);
	void Deinit(void);
protected:
	void SignalHandler(UINT8 signalMask);
	std::string _dbus_name;
};

#endif	// __MEDIACTRL_DBUS_HPP__
