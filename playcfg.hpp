#include <stdtype.h>
#include <string>

struct GeneralOptions
{
	UINT32 smplRate;
	UINT32 pbRate;
	double volume;
	UINT32 maxLoops;
	
	UINT8 resmplMode;
	UINT8 chipSmplMode;
	UINT32 chipSmplRate;
	
	UINT32 fadeTime_single;
	UINT32 fadeTime_plist;
	UINT32 pauseTime_jingle;
	UINT32 pauseTime_loop;
	
	UINT8 pbMode;	// playback mode (0 = play, 1 = log to WAV, 2 = play+log)
	bool soundWhilePaused;
	bool pseudoSurround;
	bool preferJapTag;
	UINT8 hardStopOld;
	bool fadeRawLogs;
	UINT8 showStrmCmds;
	
	std::string audDriver;
	UINT32 audOutDev;
	UINT32 audBuffers;
};
struct ChipOptions
{
	UINT8 chipType;
	UINT8 chipInstance;
	UINT8 chipDisable;	// bit mask, bit 0 (01) = main, bit 1 (02) = linked
	UINT32 emuCore;
	UINT32 emuCoreSub;
	UINT32 muteMask[2];
	UINT32 addOpts;
};

class Configuration;
class PlayerWrapper;


void ParseConfiguration(GeneralOptions& gOpts, size_t cOptCnt, ChipOptions* cOpts, const Configuration& cfg);
void ApplyCfg_General(PlayerWrapper& player, const GeneralOptions& opts);
void ApplyCfg_Chip(PlayerWrapper& player, const GeneralOptions& gOpts, const ChipOptions& cOpts);
