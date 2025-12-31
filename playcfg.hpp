#ifndef __PLAYCFG_HPP__
#define __PLAYCFG_HPP__

#include <stdtype.h>
#include <string>
#include <vector>
#include "emu/EmuStructs.h"	// for DEV_ID

struct GeneralOptions
{
	UINT32 smplRate;
	UINT8 smplBits;
	UINT32 pbRate;
	double volume;
	double pbSpeed;
	UINT32 maxLoops;
	
	UINT8 resmplMode;
	UINT8 chipSmplMode;
	UINT32 chipSmplRate;
	
	UINT32 fadeTime_single;
	UINT32 fadeTime_plist;
	UINT32 pauseTime_jingle;
	UINT32 pauseTime_loop;
	
	std::string wavLogPath;
	UINT8 pbMode;	// playback mode (0 = play, 1 = log to WAV, 2 = play+log)
	bool soundWhilePaused;
	bool pseudoSurround;
	bool preferJapTag;
	UINT8 timeDispStyle;
	bool showDevCore;
	bool setTermTitle;
	UINT8 mediaKeys;
	UINT8 hardStopOld;
	bool fadeRawLogs;
	UINT8 showStrmCmds;
	
	UINT8 logLvlFile;	// file playback
	UINT8 logLvlEmu;	// sound emulation
	
	UINT32 audDriverID;
	std::string audDriverName;
	UINT32 audOutDev;
	UINT32 audBufCnt;
	UINT32 audBufTime;
};
struct ChipOptions
{
	UINT8 chipType;
	UINT8 chipInstance;
	UINT8 chipDisable;	// bit mask, bit 0 (01) = main, bit 1 (02) = linked
	UINT32 emuCore;
	UINT32 emuCoreSub;
	UINT32 muteMask[2];
	double panMask[2][32];
	UINT32 addOpts;
};

class Configuration;
class PlayerA;


std::vector<DEV_ID> GetConfigurableDevices(void);
void ParseConfiguration(GeneralOptions& gOpts, size_t cOptCnt, ChipOptions* cOpts, const Configuration& cfg);
void ApplyCfg_General(PlayerA& player, const GeneralOptions& opts);
void ApplyCfg_Chip(PlayerA& player, const GeneralOptions& gOpts, const ChipOptions& cOpts);

#endif	// __PLAYCFG_HPP__
