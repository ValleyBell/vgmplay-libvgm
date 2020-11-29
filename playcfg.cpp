#include <string.h>
#include <vector>
#include <string>

#ifdef _MSC_VER
#define stricmp		_stricmp
#define strnicmp	_strnicmp
#else
#define stricmp		strcasecmp
#define strnicmp	strncasecmp
#endif

#include <stdtype.h>
#include <common_def.h>	// for INLINE
#include <player/playerbase.hpp>
#include <player/s98player.hpp>
#include <player/droplayer.hpp>
#include <player/vgmplayer.hpp>
#include <emu/SoundDevs.h>
// libvgm-emu headers for configuration
#include <emu/EmuCores.h>
#include <emu/EmuStructs.h>	// for DEVRI_SRMODE_* constants
#include <emu/cores/2612intf.h>
#include <emu/cores/gb.h>
#include <emu/cores/nesintf.h>
#include <emu/cores/okim6258.h>
#include <emu/cores/scsp.h>
#include <emu/cores/c352.h>

#include "config.hpp"
#include "playcfg.hpp"
#include "player.hpp"


struct ChipCfgSectDef
{
	UINT8 chipType;
	const char* entryName;
};

#define BIT_MASK(startBit, bitCnt)	(((1 << bitCnt) - 1) << startBit)

INLINE UINT32 MulDivRoundU32(UINT32 val, UINT32 mul, UINT32 div)
{
	return (UINT32)(((UINT64)val * mul + (div / 2)) / div);
}


//void ParseConfiguration(GeneralOptions& gOpts, size_t cOptCnt, ChipOptions* cOpts, const Configuration& cfg);
static inline UINT32 Str2FCC(const std::string& fcc);
static inline std::string Cfg_GetStrOrDefault(const CfgSection::Unordered& ceList, const std::string& entryName, std::string defaultValue);
static inline unsigned long Cfg_GetUIntOrDefault(const CfgSection::Unordered& ceList, const std::string& entryName, unsigned long defaultValue);
static inline double Cfg_GetFloatOrDefault(const CfgSection::Unordered& ceList, const std::string& entryName, double defaultValue);
static inline bool Cfg_GetBoolOrDefault(const CfgSection::Unordered& ceList, const std::string& entryName, bool defaultValue);
static std::vector<std::string> Cfg_Str2VectStr(const std::string& text);
static void ParseCfg_General(GeneralOptions& opts, const CfgSection& cfg);
static void ParseCfg_ChipSection(ChipOptions& opts, const CfgSection& cfg, UINT8 chipType);

//void ApplyCfg_General(PlayerWrapper& player, const GeneralOptions& opts);
//void ApplyCfg_Chip(PlayerWrapper& player, const GeneralOptions& gOpts, const ChipOptions& cOpts);


static const ChipCfgSectDef CFG_CHIP_LIST[] =
{
	{	DEVID_SN76496,	"SN76496"},
	{	DEVID_YM2413,	"YM2413"},
	{	DEVID_YM2612,	"YM2612"},
	{	DEVID_YM2151,	"YM2151"},
	{	DEVID_SEGAPCM,	"SegaPCM"},
	{	DEVID_RF5C68,	"RF5C68"},
	{	DEVID_YM2203,	"YM2203"},
	{	DEVID_YM2608,	"YM2608"},
	{	DEVID_YM2610,	"YM2610"},
	{	DEVID_YM3812,	"YM3812"},
	{	DEVID_YM3526,	"YM3526"},
	{	DEVID_Y8950,	"Y8950"},
	{	DEVID_YMF262,	"YMF262"},
	{	DEVID_YMF278B,	"YMF278B"},
	{	DEVID_YMF271,	"YMF271"},
	{	DEVID_YMZ280B,	"YMZ280B"},
	//{	DEVID_RF5C68,	"RF5C164"},
	{	DEVID_32X_PWM,	"PWM"},
	{	DEVID_AY8910 ,	"AY8910"},
	{	DEVID_GB_DMG,	"GameBoy"},
	{	DEVID_NES_APU,	"NES APU"},
	{	DEVID_YMW258,	"YMW258"},
	{	DEVID_uPD7759,	"uPD7759"},
	{	DEVID_OKIM6258,	"OKIM6258"},
	{	DEVID_OKIM6295,	"OKIM6295"},
	{	DEVID_K051649,	"K051649"},
	{	DEVID_K054539,	"K054539"},
	{	DEVID_C6280,	"HuC6280"},
	{	DEVID_C140,		"C140"},
	{	DEVID_C219,		"C219"},
	{	DEVID_K053260,	"K053260"},
	{	DEVID_POKEY,	"Pokey"},
	{	DEVID_QSOUND,	"QSound"},
	{	DEVID_SCSP,		"SCSP"},
	{	DEVID_WSWAN,	"WSwan"},
	{	DEVID_VBOY_VSU,	"VSU"},
	{	DEVID_SAA1099,	"SAA1099"},
	{	DEVID_ES5503,	"ES5503"},
	{	DEVID_ES5506,	"ES5506"},
	{	DEVID_X1_010,	"X1-010"},
	{	DEVID_C352,		"C352"},
	{	DEVID_GA20,		"GA20"},
};
static const size_t CFG_CHIP_COUNT = sizeof(CFG_CHIP_LIST) / sizeof(CFG_CHIP_LIST[0]);


void ParseConfiguration(GeneralOptions& gOpts, size_t cOptCnt, ChipOptions* cOpts, const Configuration& cfg)
{
	Configuration::SectList::const_iterator sectIt;
	CfgSection dummySect;
	size_t curChp;
	
	sectIt = cfg._sections.find("General");
	if (sectIt != cfg._sections.end())
		ParseCfg_General(gOpts, sectIt->second);
	else
		ParseCfg_General(gOpts, dummySect);
	
	for (curChp = 0; curChp < cOptCnt; curChp ++)
		cOpts[curChp].chipType = 0xFF;
	for (curChp = 0; curChp < CFG_CHIP_COUNT; curChp ++)
	{
		const ChipCfgSectDef& cfgChip = CFG_CHIP_LIST[curChp];
		sectIt = cfg._sections.find(cfgChip.entryName);
		if (sectIt != cfg._sections.end())
			ParseCfg_ChipSection(cOpts[cfgChip.chipType], sectIt->second, cfgChip.chipType);
		else
			ParseCfg_ChipSection(cOpts[cfgChip.chipType], dummySect, cfgChip.chipType);
	}
	
	return;
}

static inline UINT32 Str2FCC(const std::string& fcc)
{
	char buf[4];
	strncpy(buf, fcc.c_str(), 4);
	return	(buf[0] << 24) | (buf[1] << 16) |
			(buf[2] <<  8) | (buf[3] <<  0);
}

static inline std::string Cfg_GetStrOrDefault(const CfgSection::Unordered& ceList, const std::string& entryName, std::string defaultValue)
{
	CfgSection::Unordered::const_iterator ceIt = ceList.find(entryName);
	return (ceIt != ceList.end()) ? Configuration::ToString(ceIt->second) : defaultValue;
}

static inline unsigned long Cfg_GetUIntOrDefault(const CfgSection::Unordered& ceList, const std::string& entryName, unsigned long defaultValue)
{
	CfgSection::Unordered::const_iterator ceIt = ceList.find(entryName);
	return (ceIt != ceList.end()) ? Configuration::ToUInt(ceIt->second) : defaultValue;
}

static inline double Cfg_GetFloatOrDefault(const CfgSection::Unordered& ceList, const std::string& entryName, double defaultValue)
{
	CfgSection::Unordered::const_iterator ceIt = ceList.find(entryName);
	return (ceIt != ceList.end()) ? Configuration::ToFloat(ceIt->second) : defaultValue;
}

static inline bool Cfg_GetBoolOrDefault(const CfgSection::Unordered& ceList, const std::string& entryName, bool defaultValue)
{
	CfgSection::Unordered::const_iterator ceIt = ceList.find(entryName);
	return (ceIt != ceList.end()) ? Configuration::ToBool(ceIt->second) : defaultValue;
}

static std::vector<std::string> Cfg_Str2VectStr(const std::string& text)
{
	size_t pos1;
	size_t pos2;
	std::vector<std::string> result;
	
	pos1 = 0;
	pos2 = text.find(',', pos1);
	while(pos2 != std::string::npos)
	{
		result.push_back(text.substr(pos1, pos2 - pos1));
		pos1 = pos2 + 1;
		pos2 = text.find(',', pos1);
	}
	result.push_back(text.substr(pos1));
	return result;
}

static void ParseCfg_General(GeneralOptions& opts, const CfgSection& cfg)
{
	const CfgSection::Unordered& ceList = cfg.unord;
	CfgSection::Unordered::const_iterator ceIt;	// config entry iterator
	
	opts.smplRate =		(UINT32)Cfg_GetUIntOrDefault(ceList, "SampleRate", 44100);
	opts.pbRate =		(UINT32)Cfg_GetUIntOrDefault(ceList, "PlaybackRate", 0);
	opts.volume =		(double)Cfg_GetFloatOrDefault(ceList, "Volume", 1.0);
	opts.maxLoops =		(UINT32)Cfg_GetUIntOrDefault(ceList, "MaxLoops", 2);
	
	opts.resmplMode =	 (UINT8)Cfg_GetUIntOrDefault(ceList, "ResamplingMode", 0);
	opts.chipSmplMode =	 (UINT8)Cfg_GetUIntOrDefault(ceList, "ChipSmplMode", 0);
	opts.chipSmplRate =	(UINT32)Cfg_GetUIntOrDefault(ceList, "ChipSmplRate", 0);
	
	opts.fadeTime_single =	(UINT32)Cfg_GetUIntOrDefault(ceList, "FadeTime", 5000);
	opts.fadeTime_plist =	(UINT32)Cfg_GetUIntOrDefault(ceList, "FadeTimePL", 2000);
	opts.pauseTime_jingle =	(UINT32)Cfg_GetUIntOrDefault(ceList, "JinglePause", 1000);
	opts.pauseTime_loop =	(UINT32)Cfg_GetUIntOrDefault(ceList, "FadePause", 0);
	
	opts.pbMode =			 (UINT8)Cfg_GetUIntOrDefault(ceList, "LogSound", 0);
	opts.soundWhilePaused =	  (bool)Cfg_GetBoolOrDefault(ceList, "EmulatePause", false);
	opts.pseudoSurround =	  (bool)Cfg_GetBoolOrDefault(ceList, "SurroundSound", false);
	opts.preferJapTag =		  (bool)Cfg_GetBoolOrDefault(ceList, "PreferJapTag", false);
	opts.showDevCore =		  (bool)Cfg_GetBoolOrDefault(ceList, "ShowChipCore", false);
	opts.setTermTitle =		  (bool)Cfg_GetBoolOrDefault(ceList, "SetTerminalTitle", true);
	{
		std::string hsStr = Cfg_GetStrOrDefault(ceList, "HardStopOld", "0");
		if (isdigit((unsigned char)hsStr[0]))
			opts.hardStopOld = (UINT8)Configuration::ToUInt(hsStr);
		else
			opts.hardStopOld = Configuration::ToBool(hsStr) ? 1 : 0;
	}
	opts.fadeRawLogs =		  (bool)Cfg_GetBoolOrDefault(ceList, "FadeRAWLogs", false);
	opts.showStrmCmds =		 (UINT8)Cfg_GetUIntOrDefault(ceList, "ShowStreamCmds", 0);
	opts.audDriverName =	        Cfg_GetStrOrDefault (ceList, "AudioDriver", "");
	if (! opts.audDriverName.empty() && isdigit((unsigned char)opts.audDriverName[0]))
		opts.audDriverID = (UINT32)Configuration::ToUInt(opts.audDriverName);	// TOOD: make this conversion safer
	else
		opts.audDriverID = (UINT32)-1;
	opts.audBufCnt =		(UINT32)Cfg_GetUIntOrDefault(ceList, "AudioBuffers", 0);
	opts.audBufTime =		(UINT32)Cfg_GetUIntOrDefault(ceList, "AudioBufferSize", 0);
	opts.audOutDev =		(UINT32)Cfg_GetUIntOrDefault(ceList, "OutputDevice", 0);
	
	return;
}

static void ParseCfg_ChipSection(ChipOptions& opts, const CfgSection& cfg, UINT8 chipType)
{
	const CfgSection::Unordered& ceuList = cfg.unord;
	const CfgSection::Ordered& ceoList = cfg.ordered;
	CfgSection::Unordered::const_iterator ceuIt;	// config entry iterator (unordered)
	CfgSection::Ordered::const_iterator ceoIt;		// config entry iterator (ordered)
	
	opts.chipType = chipType;
	opts.chipInstance = 0xFF;
	
	// parse muting options
	memset(&opts.muteMask[0], 0x00, sizeof(opts.muteMask));
	memset(&opts.panMask[0][0], 0x00, sizeof(opts.panMask));
	for (ceoIt = ceoList.begin(); ceoIt != ceoList.end(); ++ceoIt)
	{
		const std::pair<std::string, std::string>& ce = *ceoIt;
		const char* key = ce.first.c_str();
		const char* value = ce.second.c_str();
		if (! stricmp(key, "MuteMask"))
		{
			if (chipType == DEVID_YMF278B)
				continue;
			opts.muteMask[0] = (UINT32)strtoul(value, NULL, 0);
		}
		else if (! strnicmp(key, "MuteMask_", 9))
		{
			const char* maskSpecifier = &key[9];
			switch(chipType)
			{
			case DEVID_YM2203:
				if (! stricmp(maskSpecifier, "FM"))
					opts.muteMask[0] = (UINT32)strtoul(value, NULL, 0);
				else if (! stricmp(maskSpecifier, "SSG"))
					opts.muteMask[1] = (UINT32)strtoul(value, NULL, 0);
				break;
			case DEVID_YM2608:
			case DEVID_YM2610:
				if (! stricmp(maskSpecifier, "FM"))
				{
					opts.muteMask[0] &= ~BIT_MASK(0, 6);
					opts.muteMask[0] |= ((UINT32)strtoul(value, NULL, 0) & BIT_MASK(0, 6)) << 0;
				}
				else if (! stricmp(maskSpecifier, "PCM"))
				{
					opts.muteMask[0] &= ~BIT_MASK(6, 7);
					opts.muteMask[0] |= ((UINT32)strtoul(value, NULL, 0) & BIT_MASK(0, 7)) << 6;
				}
				else if (! stricmp(maskSpecifier, "SSG"))
				{
					opts.muteMask[1] = (UINT32)strtoul(value, NULL, 0);
				}
				break;
			case DEVID_YMF278B:
				if (! stricmp(maskSpecifier, "FM"))
					opts.muteMask[1] = (UINT32)strtoul(value, NULL, 0);
				else if (! stricmp(maskSpecifier, "WT"))
					opts.muteMask[0] = (UINT32)strtoul(value, NULL, 0);
				break;
			}
		}	// end if (key == "MuteMask_*")
		else if (! strnicmp(key, "Mute", 4))
		{
			const char* chnName = &key[4];
			char* endPtr = NULL;
			UINT8 chnNum = 0xFF;
			UINT8 maskID = 0xFF;
			if (! strnicmp(chnName, "Ch", 2))
			{
				chnNum = (UINT8)strtol(&chnName[2], &endPtr, 0);
				if (endPtr == NULL || endPtr == &chnName[2])
					continue;
				if (chipType == DEVID_YMF278B)
					continue;
				maskID = 0;
			}
			else
			{
				switch(chipType)
				{
				case DEVID_YM2612:
					if (! stricmp(chnName, "DAC"))
					{
						chnNum = 6;
						maskID = 0;
					}
					break;
				case DEVID_YM2203:
					if (! strnicmp(chnName, "FMCh", 4))
					{
						const char* idStr = &chnName[4];
						chnNum = (UINT8)strtol(idStr, &endPtr, 0);
						if (endPtr == NULL || endPtr == idStr)
							break;
						maskID = 0;
					}
					else if (! strnicmp(chnName, "SSGCh", 5))
					{
						const char* idStr = &chnName[5];
						chnNum = (UINT8)strtol(idStr, &endPtr, 0);
						if (endPtr == NULL || endPtr == idStr)
							break;
						maskID = 1;
					}
					break;
				case DEVID_YM2608:
				case DEVID_YM2610:
					if (! strnicmp(chnName, "FMCh", 4))
					{
						const char* idStr = &chnName[4];
						chnNum = (UINT8)strtol(idStr, &endPtr, 0);
						if (endPtr == NULL || endPtr == idStr)
							break;
						if (chnNum >= 6)
							break;
						chnNum += 0;
						maskID = 0;
					}
					else if (! strnicmp(chnName, "PCMCh", 5))
					{
						const char* idStr = &chnName[5];
						chnNum = (UINT8)strtol(idStr, &endPtr, 0);
						if (endPtr == NULL || endPtr == idStr)
							break;
						if (chnNum >= 6)
							break;
						chnNum += 6;
						maskID = 0;
					}
					else if (! stricmp(chnName, "DT"))
					{
						chnNum = 12;
						maskID = 0;
					}
					else if (! strnicmp(chnName, "SSGCh", 5))
					{
						const char* idStr = &chnName[5];
						chnNum = (UINT8)strtol(idStr, &endPtr, 0);
						if (endPtr == NULL || endPtr == idStr)
							break;
						maskID = 1;
					}
					break;
				case DEVID_YM2413:
				case DEVID_YM3812:
				case DEVID_YM3526:
				case DEVID_Y8950:
				case DEVID_YMF262:
					if (! stricmp(chnName, "BD"))
						chnNum = 0;
					else if (! stricmp(chnName, "SD"))
						chnNum = 1;
					else if (! stricmp(chnName, "TOM"))
						chnNum = 2;
					else if (! stricmp(chnName, "TC"))
						chnNum = 3;
					else if (! stricmp(chnName, "HH"))
						chnNum = 4;
					else if (chipType == DEVID_Y8950 && ! stricmp(chnName, "DT"))
						chnNum = 5;
					if (chnNum != 0xFF)
					{
						chnNum += (chipType == DEVID_YMF262) ? 18 : 9;
						maskID = 0;
					}
					break;
				case DEVID_YMF278B:
					if (! strnicmp(chnName, "FMCh", 4))
					{
						const char* idStr = &chnName[4];
						chnNum = (UINT8)strtol(idStr, &endPtr, 0);
						if (endPtr == NULL || endPtr == idStr)
							break;
						if (chnNum >= 18)
							break;
						maskID = 1;
					}
					else if (! strnicmp(chnName, "WTCh", 4))
					{
						const char* idStr = &chnName[4];
						chnNum = (UINT8)strtol(idStr, &endPtr, 0);
						if (endPtr == NULL || endPtr == idStr)
							continue;
						maskID = 0;
					}
					else if (! strnicmp(chnName, "FM", 2))
					{
						chnName += 2;
						if (! stricmp(chnName, "BD"))
							chnNum = 0;
						else if (! stricmp(chnName, "SD"))
							chnNum = 1;
						else if (! stricmp(chnName, "TOM"))
							chnNum = 2;
						else if (! stricmp(chnName, "TC"))
							chnNum = 3;
						else if (! stricmp(chnName, "HH"))
							chnNum = 4;
						if (chnNum != 0xFF)
						{
							chnNum += 18;
							maskID = 1;
						}
					}
					break;
				}
			}
			if (maskID != 0xFF)
			{
				if (Configuration::ToBool(value))
					opts.muteMask[maskID] |= (1 << chnNum);
				else
					opts.muteMask[maskID] &= ~(1 << chnNum);
			}
		}	// end if (key == "Mute*")
		else if (! stricmp(key, "PanMask"))
		{
			if (chipType == DEVID_YMF278B)
				continue;
			std::vector<std::string> panStrs = Cfg_Str2VectStr(value);
			size_t curChn;
			size_t chnCnt = sizeof(opts.panMask[0]) / sizeof(opts.panMask[0][0]);
			for (curChn = 0; curChn < panStrs.size() && curChn < chnCnt; curChn ++)
				opts.panMask[0][curChn] = strtod(panStrs[curChn].c_str(), NULL);
		}
		else if (! strnicmp(key, "PanMask_", 8))
		{
			const char* maskSpecifier = &key[8];
			UINT8 maskID = 0xFF;
			size_t chnStart = 0;
			size_t chnCnt = sizeof(opts.panMask[0]) / sizeof(opts.panMask[0][0]);
			switch(chipType)
			{
			case DEVID_YM2203:
				if (! stricmp(maskSpecifier, "FM"))
					maskID = 0;
				else if (! stricmp(maskSpecifier, "SSG"))
					maskID = 1;
				break;
			case DEVID_YM2608:
			case DEVID_YM2610:
				if (! stricmp(maskSpecifier, "FM"))
				{
					maskID = 0;
					chnStart = 0;	chnCnt = 6;
				}
				else if (! stricmp(maskSpecifier, "PCM"))
				{
					maskID = 0;
					chnStart = 6;	chnCnt = 7;
				}
				else if (! stricmp(maskSpecifier, "SSG"))
				{
					maskID = 1;
				}
				break;
			case DEVID_YMF278B:
				if (! stricmp(maskSpecifier, "FM"))
					maskID = 1;
				else if (! stricmp(maskSpecifier, "WT"))
					maskID = 0;
				break;
			}
			
			if (maskID != 0xFF)
			{
				std::vector<std::string> panStrs = Cfg_Str2VectStr(value);
				size_t curChn;
				for (curChn = 0; curChn < panStrs.size() && curChn < chnCnt; curChn ++)
					opts.panMask[maskID][chnStart + curChn] = strtod(panStrs[curChn].c_str(), NULL);
			}
		}	// end if (key == "PanMask_*")
	}	// end for (ceoIt)
	
	opts.chipDisable = Cfg_GetBoolOrDefault(ceuList, "Disabled", false) ? 0x01 : 0x00;
	{
		UINT8 emuType = (UINT8)Cfg_GetUIntOrDefault(ceuList, "EmulatorType", 0xFF);
		// select emuCore based on number
		switch(chipType)
		{
		case DEVID_SN76496:
			if (emuType == 0)
				opts.emuCore = FCC_MAME;
			else if (emuType == 1)
				opts.emuCore = FCC_MAXM;
			break;
		case DEVID_YM2413:
			if (emuType == 0)
				opts.emuCore = FCC_EMU_;
			else if (emuType == 1)
				opts.emuCore = FCC_MAME;
			else if (emuType == 2)
				opts.emuCore = FCC_NUKE;
			break;
		case DEVID_YM2612:
			if (emuType == 0)
				opts.emuCore = FCC_GPGX;
			else if (emuType == 1)
				opts.emuCore = FCC_NUKE;
			else if (emuType == 2)
				opts.emuCore = FCC_GENS;
			break;
		case DEVID_YM2151:
			if (emuType == 0)
				opts.emuCore = FCC_MAME;
			else if (emuType == 1)
				opts.emuCore = FCC_NUKE;
			break;
		case DEVID_YM2203:
		case DEVID_YM2608:
		case DEVID_YM2610:
			if (emuType == 0)
				opts.emuCoreSub = FCC_EMU_;
			else if (emuType == 1)
				opts.emuCoreSub = FCC_MAME;
			break;
		case DEVID_YM3812:
			if (emuType == 0)
				opts.emuCore = FCC_ADLE;
			else if (emuType == 1)
				opts.emuCore = FCC_MAME;
			break;
		case DEVID_YMF262:
			if (emuType == 0)
				opts.emuCore = FCC_ADLE;
			else if (emuType == 1)
				opts.emuCore = FCC_MAME;
			else if (emuType == 2)
				opts.emuCore = FCC_NUKE;
			break;
		case DEVID_AY8910:
			if (emuType == 0)
				opts.emuCore = FCC_EMU_;
			else if (emuType == 1)
				opts.emuCore = FCC_MAME;
			break;
		case DEVID_NES_APU:
			if (emuType == 0)
				opts.emuCore = FCC_NSFP;
			else if (emuType == 1)
				opts.emuCore = FCC_MAME;
			break;
		case DEVID_C6280:
			if (emuType == 0)
				opts.emuCore = FCC_OOTK;
			else if (emuType == 1)
				opts.emuCore = FCC_MAME;
			break;
		case DEVID_QSOUND:
			if (emuType == 0)
				opts.emuCore = FCC_CTR_;
			else if (emuType == 1)
				opts.emuCore = FCC_MAME;
		case DEVID_SAA1099:
			if (emuType == 0)
				opts.emuCore = FCC_VBEL;
			else if (emuType == 1)
				opts.emuCore = FCC_MAME;
			break;
		}	// end switch(chipType) for emuCore
	}
	{
		std::string coreStr = Cfg_GetStrOrDefault(ceuList, "Core", "");
		if (! coreStr.empty())
			opts.emuCore = Str2FCC(coreStr);
	}
	
	// chip-specific options
	switch(chipType)
	{
	case DEVID_YM2612:
		opts.addOpts  = Cfg_GetBoolOrDefault(ceuList, "PseudoStereo", false) ? OPT_YM2612_PSEUDO_STEREO : 0x00;
		opts.addOpts |= Cfg_GetBoolOrDefault(ceuList, "DACHighpass", false) ? OPT_YM2612_DAC_HIGHPASS : 0x00;
		opts.addOpts |= Cfg_GetBoolOrDefault(ceuList, "SSG-EG", false) ? OPT_YM2612_SSGEG : 0x00;
		opts.addOpts |= (Cfg_GetUIntOrDefault(ceuList, "NukedType", 0) & 0x03) << 4;
		break;
	case DEVID_YM2203:
	case DEVID_YM2608:
	case DEVID_YM2610:
		opts.chipDisable |= Cfg_GetBoolOrDefault(ceuList, "DisableAY", false) ? 0x02 : 0x00;
		{
			std::string coreStr = Cfg_GetStrOrDefault(ceuList, "CoreSSG", "");
			if (! coreStr.empty())
				opts.emuCoreSub = Str2FCC(coreStr);
		}
		break;
	case DEVID_YMF278B:
		{
			std::string coreStr = Cfg_GetStrOrDefault(ceuList, "CoreFM", "");
			if (! coreStr.empty())
				opts.emuCoreSub = Str2FCC(coreStr);
		}
		break;
	case DEVID_GB_DMG:
		opts.addOpts = Cfg_GetBoolOrDefault(ceuList, "BoostWaveChn", true) ? OPT_GB_DMG_BOOST_WAVECH : 0x00;
		break;
	case DEVID_NES_APU:
		opts.addOpts  = (Cfg_GetUIntOrDefault(ceuList, "SharedOpts", 0x03) & 0x03) << 0;
		opts.addOpts |= (Cfg_GetUIntOrDefault(ceuList, "APUOpts", 0x01) & 0x03) << 2;
		opts.addOpts |= (Cfg_GetUIntOrDefault(ceuList, "DMCOpts", 0x3B) & 0x3F) << 4;
		opts.addOpts |= (Cfg_GetUIntOrDefault(ceuList, "FDSOpts", 0x03) & 0x03) << 10;
		break;
	case DEVID_OKIM6258:
		opts.addOpts = Cfg_GetBoolOrDefault(ceuList, "Enable10Bit", false) ? 0x00 : OPT_OKIM6258_FORCE_12BIT;
		break;
	case DEVID_SCSP:
		opts.addOpts = Cfg_GetBoolOrDefault(ceuList, "BypassDSP", true) ? OPT_SCSP_BYPASS_DSP : 0x00;
		break;
	case DEVID_C352:
		opts.addOpts = Cfg_GetBoolOrDefault(ceuList, "DisableRear", false) ? OPT_C352_MUTE_REAR : 0x00;
		break;
	}	// end switch(chipType) for chip-specific options
	
	return;
}


void ApplyCfg_General(PlayerWrapper& player, const GeneralOptions& opts)
{
	const std::vector<PlayerBase*>& plrs = player.GetRegisteredPlayers();
	size_t curPlr;
	UINT8 retVal;
	PlrWrapConfig pwCfg;
	
	pwCfg = player.GetConfiguration();
	pwCfg.masterVol = (INT32)(0x10000 * opts.volume + 0.5);
	pwCfg.chnInvert = opts.pseudoSurround ? 0x02 : 0x00;
	pwCfg.loopCount = opts.maxLoops;
	pwCfg.fadeSmpls = MulDivRoundU32(opts.fadeTime_single, opts.smplRate, 1000);
	pwCfg.endSilenceSmpls = MulDivRoundU32(opts.pauseTime_jingle, opts.smplRate, 1000);
	pwCfg.pbSpeed = 1.0;
	player.SetConfiguration(pwCfg);
	
	for (curPlr = 0; curPlr < plrs.size(); curPlr ++)
	{
		PlayerBase* pBase = plrs[curPlr];
		switch(pBase->GetPlayerType())
		{
		case FCC_VGM:
		{
			VGMPlayer* vgmplay = dynamic_cast<VGMPlayer*>(pBase);
			VGM_PLAY_OPTIONS vpOpts;
			retVal = vgmplay->GetPlayerOptions(vpOpts);
			if (retVal)
				break;
			
			vpOpts.playbackHz = opts.pbRate;
			vpOpts.hardStopOld = opts.hardStopOld;
			vgmplay->SetPlayerOptions(vpOpts);
		}
			break;
		}
	}
	
	return;
}

static UINT8 ConvertChipSmplModeOption(UINT8 devID, UINT8 option)
{
	switch(option)
	{
	case 0x00:	// native
		return DEVRI_SRMODE_NATIVE;
	case 0x01:	// highest sampling rate (native/custom)
		return DEVRI_SRMODE_HIGHEST;
	case 0x02:	// custom sample rate
		return DEVRI_SRMODE_CUSTOM;
	case 0x03:	// native for FM, highest for others
		{
			bool isFM = false;
			isFM = (devID == DEVID_YM3526 || devID == DEVID_Y8950 || devID == DEVID_YM3812 ||
				devID == DEVID_YM2413 || devID == DEVID_YMF262 || devID == DEVID_YM2151 ||
				devID == DEVID_YM2203 || devID == DEVID_YM2608 || devID == DEVID_YM2610 || devID == DEVID_YM2612);
			return isFM ? DEVRI_SRMODE_NATIVE : DEVRI_SRMODE_HIGHEST;
		}
	default:
		return 0x00;
	}
}

void ApplyCfg_Chip(PlayerWrapper& player, const GeneralOptions& gOpts, const ChipOptions& cOpts)
{
	const std::vector<PlayerBase*>& plrs = player.GetRegisteredPlayers();
	size_t curPlr;
	UINT8 retVal;
	UINT32 devID;
	
	if (cOpts.chipInstance != 0xFF)
		devID = PLR_DEV_ID(cOpts.chipType, cOpts.chipInstance);
	else
		devID = PLR_DEV_ID(cOpts.chipType, 0x00);
	for (curPlr = 0; curPlr < plrs.size(); curPlr ++)
	{
		PlayerBase* pBase = plrs[curPlr];
		PLR_DEV_OPTS devOpts;
		UINT8 curInst;
		UINT8 curChn;
		
		retVal = pBase->GetDeviceOptions(devID, devOpts);
		if (retVal)
			continue;	// this player doesn't support this chip
		
		devOpts.emuCore[0] = cOpts.emuCore;
		devOpts.emuCore[1] = cOpts.emuCoreSub;
		devOpts.srMode = ConvertChipSmplModeOption(cOpts.chipType, gOpts.chipSmplMode);
		devOpts.resmplMode = gOpts.resmplMode;
		devOpts.smplRate = gOpts.chipSmplRate;
		devOpts.coreOpts = cOpts.addOpts;
		devOpts.muteOpts.disable = cOpts.chipDisable;
		for (curInst = 0; curInst < 2; curInst ++)
		{
			devOpts.muteOpts.chnMute[curInst] = cOpts.muteMask[curInst];
			for (curChn = 0; curChn < 32; curChn ++)
				devOpts.panOpts.chnPan[curInst][curChn] = (INT16)(0x100 * cOpts.panMask[curInst][curChn]);
		}
		//printf("Player %s: Setting chip options for chip 0x%02X, instance 0x%02X\n",
		//		pBase->GetPlayerName(), cOpts.chipType, cOpts.chipInstance);
		if (cOpts.chipInstance != 0xFF)
		{
			pBase->SetDeviceOptions(devID, devOpts);
		}
		else
		{
			for (curInst = 0; curInst < 2; curInst ++)
				pBase->SetDeviceOptions(PLR_DEV_ID(cOpts.chipType, curInst), devOpts);
		}
	}
	
	return;
}
