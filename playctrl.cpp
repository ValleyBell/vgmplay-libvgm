#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <vector>
#include <string>
#include <math.h>

#ifdef _WIN32
//#define _WIN32_WINNT	0x500	// for GetConsoleWindow()
#include <windows.h>
extern "C" int __cdecl _getch(void);	// from conio.h
extern "C" int __cdecl _kbhit(void);
#else
#include <unistd.h>		// for STDIN_FILENO and usleep()
#include <termios.h>
#include <sys/time.h>	// for struct timeval in _kbhit()
#define	Sleep(msec)	usleep(msec * 1000)
#endif

#ifdef _MSC_VER
#define snprintf	_snprintf
#endif

#include <stdtype.h>
#include <common_def.h>	// for INLINE
#include <utils/DataLoader.h>
#include <utils/FileLoader.h>
#include <utils/MemoryLoader.h>
#include <player/playerbase.hpp>
#include <player/s98player.hpp>
#include <player/droplayer.hpp>
#include <player/vgmplayer.hpp>
#include <audio/AudioStream.h>
#include <audio/AudioStream_SpcDrvFuns.h>
#include <emu/SoundDevs.h>
#include <emu/SoundEmu.h>	// for SndEmu_GetDevName()
#include <utils/OSMutex.h>

#include "utils.hpp"
#include "config.hpp"
#include "m3uargparse.hpp"
#include "playcfg.hpp"
#include "player.hpp"


struct AudioDriver
{
	UINT8 driverType;		// ADRVTYPE_OUT or ADRVTYPE_DISK
	INT32 dTypeID;			// [config] choose audio driver number N of driverType (-1 = by name)
	std::string dTypeName;	// [config] choose audio driver by name (empty: don't use)
	INT32 driverID;			// actual ID of the audio driver
	INT32 deviceID;			// [config] driver device ID (0 = default)
	void* data;				// data structure
};


UINT8 PlayerMain(UINT8 showFileName);
static UINT8 OpenFile(const std::string& fileName, DATA_LOADER*& dLoad, PlayerBase*& player);
static const char* GetTagForDisp(const std::map<std::string, std::string>& tags, const std::string& tagName);
static void ShowSongInfo(const std::string& fileName);
static void ShowConsoleTitle(const std::string& fileName, const std::string& titleTag, const std::string& gameTag);
static UINT8 PlayFile(void);
static int GetPressedKey(void);
static UINT8 HandleKeyPress(void);
static inline std::string FCC2Str(UINT32 fcc);
static INT8 GetTimeDispMode(double seconds);
static std::string GetTimeStr(double seconds, INT8 showHours = 0);
static UINT32 FillBuffer(void* drvStruct, void* userParam, UINT32 bufSize, void* Data);
static UINT32 FillBufferDummy(void* drvStruct, void* userParam, UINT32 bufSize, void* data);
static UINT8 FilePlayCallback(PlayerBase* player, void* userParam, UINT8 evtType, void* evtParam);
static DATA_LOADER* PlayerFileReqCallback(void* userParam, PlayerBase* player, const char* fileName);
static UINT8 ChooseAudioDriver(AudioDriver* aDrv);
static UINT8 InitAudioDriver(AudioDriver* aDrv);
static UINT8 InitAudioSystem(void);
static UINT8 DeinitAudioSystem(void);
static UINT8 StartAudioDevice(void);
static UINT8 StopAudioDevice(void);
static UINT8 StartDiskWriter(const std::string& songFileName);
static UINT8 StopDiskWriter(void);
#ifndef _WIN32
static void changemode(UINT8 noEcho);
static int _kbhit(void);
#define	_getch	getchar
#endif
static void cls(void);


#define APP_NAME	"VGM Player"
#define APP_NAME_L	L"VGM Player"

// NCurses-like key constants
#define KEY_DOWN		0x102		// cursor down
#define KEY_UP			0x103		// cursor up
#define KEY_LEFT		0x104		// cursor left
#define KEY_RIGHT		0x105		// cursor right
#define KEY_NPAGE		0x152		// next page (page down)
#define KEY_PPAGE		0x153		// previouspage (page up)
#define KEY_MASK		0xFFF
#define KEY_CTRL		0x1000
#define KEY_SHIFT		0x2000
#define KEY_ALT			0x4000


static AudioDriver adOut = {ADRVTYPE_OUT, -1, "", 0, 0, NULL};
static AudioDriver adLog = {ADRVTYPE_DISK, -1, "", 0, 0, NULL};

static std::vector<UINT8> audioBuf;
static OS_MUTEX* renderMtx;	// render thread mutex

static bool manualRenderLoop = false;
static bool dummyRenderAtLoad = false;
static volatile UINT8 playState;

static INT8 timeDispMode = 0;
static bool isRawLog;
static UINT32 fileSize;

extern std::vector<std::string> appSearchPaths;
extern Configuration playerCfg;
extern std::vector<SongFileList> songList;
extern std::vector<PlaylistFileList> plList;

static int controlVal;
static size_t curSong;

static GeneralOptions genOpts;
static ChipOptions chipOpts[0x100];
static PlayerWrapper myPlayer;

INLINE UINT32 MSec2Samples(UINT32 val, const PlayerWrapper& player)
{
	return (UINT32)(((UINT64)val * player.GetSampleRate() + 500) / 1000);
}

UINT8 PlayerMain(UINT8 showFileName)
{
	UINT8 retVal;
	UINT8 fnShowMode;
	
	if (songList.size() == 1 && songList[0].playlistID == (size_t)-1)
		fnShowMode = showFileName ? 1 : 2;
	else
		fnShowMode = 0;
	
	ParseConfiguration(genOpts, 0x100, chipOpts, playerCfg);
	
	retVal = InitAudioSystem();
	if (retVal)
		return 1;
	retVal = StartAudioDevice();
	if (retVal)
	{
		DeinitAudioSystem();
		return 1;
	}
	playState = 0x00;
	
	// I'll keep the instances of the players for the program's life time.
	// This way player/chip options are kept between track changes.
	myPlayer.RegisterPlayerEngine(new VGMPlayer);
	myPlayer.RegisterPlayerEngine(new S98Player);
	myPlayer.RegisterPlayerEngine(new DROPlayer);
	myPlayer.SetEventCallback(FilePlayCallback, NULL);
	myPlayer.SetFileReqCallback(&PlayerFileReqCallback, NULL);
	ApplyCfg_General(myPlayer, genOpts);
	for (size_t curChp = 0; curChp < 0x100; curChp ++)
	{
		const ChipOptions& cOpt = chipOpts[curChp];
		if (cOpt.chipType == 0xFF)
			continue;
		ApplyCfg_Chip(myPlayer, genOpts, cOpt);
	}
	
	//resVal = 0;
	controlVal = +1;	// default: next song
	for (curSong = 0; curSong < songList.size(); )
	{
		const SongFileList& sfl = songList[curSong];
		DATA_LOADER* dLoad;
		PlayerBase* player;
		
		if (fnShowMode == 1)
		{
			printf("File Name:      %s\n", sfl.fileName.c_str());
		}
		else if (fnShowMode == 0)
		{
			cls();
			printf(APP_NAME);
			printf("\n----------\n");
			if (sfl.playlistID == (size_t)-1)
			{
				printf("\n");
			}
			else
			{
				const PlaylistFileList& pfl = plList[sfl.playlistID];
				printf("Playlist File:  %s\n", pfl.fileName.c_str());
				//printf("Playlist File:  %s [song %u/%u]\n", pfl.fileName.c_str(),
				//	1 + (unsigned)sfl.playlistSongID, (unsigned)pfl.songCount);
			}
			printf("File Name:      [%*u/%u] %s\n", count_digits((int)songList.size()), 1 + (unsigned)curSong,
				(unsigned)songList.size(), sfl.fileName.c_str());
		}
		fflush(stdout);
		
		retVal = OpenFile(sfl.fileName, dLoad, player);
		if (retVal & 0x80)
		{
			// TODO: show error message + wait for key press
			if (controlVal == -1 && curSong == 0)
				controlVal = +1;
			curSong += controlVal;
			_getch();
			// TODO: evaluate pressed key + exit when ESC is pressed
			continue;
		}
		
		if (myPlayer.GetPlayer()->GetPlayerType() == FCC_VGM)
		{
			VGMPlayer* vgmplay = dynamic_cast<VGMPlayer*>(myPlayer.GetPlayer());
			const VGM_HEADER* vgmhdr = vgmplay->GetFileHeader();
			PlrWrapConfig pwCfg;
			pwCfg = myPlayer.GetConfiguration();
			pwCfg.masterVol = (INT32)(0x10000 * pow(2.0, vgmhdr->volumeGain / (double)0x100) * genOpts.volume + 0.5);
			myPlayer.SetConfiguration(pwCfg);
		}
		if (curSong + 1 == songList.size())
			myPlayer.SetFadeSamples(MSec2Samples(genOpts.fadeTime_single, myPlayer));
		else
			myPlayer.SetFadeSamples(MSec2Samples(genOpts.fadeTime_plist, myPlayer));
		if (myPlayer.GetPlayer()->GetLoopTicks() == 0)
			myPlayer.SetEndSilenceSamples(MSec2Samples(genOpts.pauseTime_jingle, myPlayer));
		else
			myPlayer.SetEndSilenceSamples(MSec2Samples(genOpts.pauseTime_loop, myPlayer));
		
		// call "start" before showing song info, so that we can get the sound cores
		myPlayer.Start();
		fileSize = DataLoader_GetSize(dLoad);
		
		timeDispMode = GetTimeDispMode(myPlayer.GetTotalTime(0));
		ShowSongInfo(sfl.fileName);
		
		retVal = StartDiskWriter(sfl.fileName);
		if (retVal)
			printf("Warning: File writer failed with error 0x%02X\n", retVal);
		PlayFile();
		StopDiskWriter();
		
		myPlayer.Stop();
		
		myPlayer.UnloadFile();
		DataLoader_Deinit(dLoad);
		
		if (controlVal == +1)
		{
			curSong ++;
		}
		else if (controlVal == -1)
		{
			if (curSong > 0)
				curSong --;
		}
		else if (controlVal == +9)
		{
			break;
		}
	}	// end for(curSong)
	
	myPlayer.UnregisterAllPlayers();
	
	StopAudioDevice();
	DeinitAudioSystem();
	
	return 0;
}

static UINT8 OpenFile(const std::string& fileName, DATA_LOADER*& dLoad, PlayerBase*& player)
{
	UINT8 retVal;
	
	dLoad = FileLoader_Init(fileName.c_str());
	if (dLoad == NULL)
		return 0xFF;
	DataLoader_SetPreloadBytes(dLoad, 0x100);
	retVal = DataLoader_Load(dLoad);
	if (retVal)
	{
		DataLoader_CancelLoading(dLoad);
		fprintf(stderr, "Error 0x%02X opening file!\n", retVal);
		return 0xFF;
	}
	retVal = myPlayer.LoadFile(dLoad);
	if (retVal)
	{
		DataLoader_CancelLoading(dLoad);
		fprintf(stderr, "Unknown file format! (Error 0x%02X)\n", retVal);
		return 0xFF;
	}
	printf("\n");
	return 0x00;
}

static void Tags_RemoveEmpty(std::map<std::string, std::string>& tags)
{
	auto tagIt = tags.begin();
	for (tagIt = tags.begin(); tagIt != tags.end(); )
	{
		if (tagIt->second.empty())
			tagIt = tags.erase(tagIt);
		else
			++ tagIt;
	}
	return;
}

static void Tags_LangFilter(std::map<std::string, std::string>& tags, const std::string& tagName, const std::vector<std::string>& langPostfixes, int defaultLang)
{
	// 1. search for matching lang-tag
	// 2. save that
	// 3. remove all others
	std::vector<std::string> langTags;
	auto chosenTagIt = tags.end();
	
	for (auto lpfIt = langPostfixes.begin(); lpfIt != langPostfixes.end(); ++lpfIt)
		langTags.push_back(tagName + *lpfIt);
	
	if (defaultLang >= 0 && (size_t)defaultLang < langTags.size())
		chosenTagIt = tags.find(langTags[defaultLang]);
	if (chosenTagIt == tags.end())
	{
		for (auto ltIt = langTags.begin(); ltIt != langTags.end(); ++ltIt)
		{
			chosenTagIt = tags.find(*ltIt);
			if (chosenTagIt != tags.end())
				break;
		}
	}
	if (chosenTagIt == tags.end())
		return;
	
	if (chosenTagIt->first != tagName)
		tags[tagName] = chosenTagIt->second;
	
	for (auto ltIt = langTags.begin(); ltIt != langTags.end(); ++ltIt)
	{
		if (*ltIt == tagName)
			continue;
		tags.erase(*ltIt);
	}
	
	return;
}

static const char* GetTagForDisp(const std::map<std::string, std::string>& tags, const std::string& tagName)
{
	auto tagIt = tags.find(tagName);
	return (tagIt == tags.end()) ? "" : tagIt->second.c_str();
}

static void ShowSongInfo(const std::string& fileName)
{
	PlayerBase* player = myPlayer.GetPlayer();
	PLR_SONG_INFO sInf;
	std::map<std::string, std::string> tags;
	std::vector<std::string> langPostfixes;
	char verStr[0x20];
	int defaultLang = genOpts.preferJapTag ? 1 : 0;
	
	langPostfixes.push_back("");
	langPostfixes.push_back("-JPN");
	
	const char* const* tagList = player->GetTags();
	for (const char* const* t = tagList; *t != NULL; t += 2)
		tags[t[0]] = t[1];
	
	Tags_RemoveEmpty(tags);	// need to remove empty VGM tags, else the LangFilter may choose them
	
	Tags_LangFilter(tags, "TITLE", langPostfixes, defaultLang);
	Tags_LangFilter(tags, "GAME", langPostfixes, defaultLang);
	Tags_LangFilter(tags, "SYSTEM", langPostfixes, defaultLang);
	Tags_LangFilter(tags, "ARTIST", langPostfixes, defaultLang);
	
	INT16 volGain = 0x00;
	player->GetSongInfo(sInf);
	isRawLog = false;	// TODO: set this variable in another function
	if (player->GetPlayerType() == FCC_VGM)
	{
		VGMPlayer* vgmplay = dynamic_cast<VGMPlayer*>(player);
		const VGM_HEADER* vgmhdr = vgmplay->GetFileHeader();
		
		sprintf(verStr, "VGM %X.%02X", (vgmhdr->fileVer >> 8) & 0xFF, (vgmhdr->fileVer >> 0) & 0xFF);
		volGain = vgmhdr->volumeGain;
		fileSize = vgmplay->GetFileHeader()->dataEnd;
		
		// RAW Log: no loop, no/empty Creator tag, System Name IS set
		if (! vgmhdr->loopOfs && tags.find("ENCODED_BY") == tags.end() &&
			tags.find("SYSTEM") != tags.end())
			isRawLog = true;
		if (! vgmhdr->numTicks)
			isRawLog = false;
	}
	else if (player->GetPlayerType() == FCC_S98)
	{
		S98Player* s98play = dynamic_cast<S98Player*>(player);
		const S98_HEADER* s98hdr = s98play->GetFileHeader();
		
		sprintf(verStr, "S98 v%u", s98hdr->fileVer);
		
		if (! s98hdr->loopOfs && tags.find("TITLE") == tags.end())
			isRawLog = true;
	}
	else if (player->GetPlayerType() == FCC_DRO)
	{
		DROPlayer* droplay = dynamic_cast<DROPlayer*>(player);
		const DRO_HEADER* drohdr = droplay->GetFileHeader();
		
		sprintf(verStr, "DRO v%u", drohdr->verMajor);	// DRO has a "verMinor" field, but it's always 0
		isRawLog = true;
	}
	
	if (genOpts.setTermTitle)
		ShowConsoleTitle(fileName, GetTagForDisp(tags, "TITLE"), GetTagForDisp(tags, "GAME"));
	
	u8printf("Track Title:    %s\n", GetTagForDisp(tags, "TITLE"));
	u8printf("Game Name:      %s\n", GetTagForDisp(tags, "GAME"));
	u8printf("System:         %s\n", GetTagForDisp(tags, "SYSTEM"));
	u8printf("Composer:       %s\n", GetTagForDisp(tags, "ARTIST"));
	if (player->GetPlayerType() == FCC_S98)
	{
		S98Player* s98play = dynamic_cast<S98Player*>(player);
		const S98_HEADER* s98hdr = s98play->GetFileHeader();
		
		u8printf("Release:        %-11s Tick Rate: %u/%u\n", GetTagForDisp(tags, "DATE"),
			s98hdr->tickMult, s98hdr->tickDiv);
	}
	else
	{
		u8printf("Release:        %s\n", GetTagForDisp(tags, "DATE"));
	}
	printf("Format:         %-11s ", verStr);
	printf("Gain:%5.2f    ", pow(2.0, volGain / (double)0x100));
	if (player->GetPlayerType() == FCC_DRO)
	{
		DROPlayer* droplay = dynamic_cast<DROPlayer*>(player);
		const DRO_HEADER* drohdr = droplay->GetFileHeader();
		const char* hwType;
		
		if (drohdr->hwType == 0)
			hwType = "OPL2";
		else if (drohdr->hwType == 1)
			hwType = "DualOPL2";
		else if (drohdr->hwType == 2)
			hwType = "OPL3";
		else
			hwType = "unknown";
		printf("HWType: %s\n", hwType);
	}
	else
	{
		UINT32 loopLen = player->GetLoopTicks();
		if (sInf.loopTick != (UINT32)-1)
			printf("Loop: Yes (%s)\n", GetTimeStr(player->Tick2Second(loopLen), -1).c_str());
		else
			printf("Loop: No\n");
	}
	u8printf("VGM by:         %s\n", GetTagForDisp(tags, "ENCODED_BY"));
	u8printf("Notes:          %s\n", GetTagForDisp(tags, "COMMENT"));
	printf("\n");
	
	std::vector<PLR_DEV_INFO> diList;
	player->GetSongDeviceInfo(diList);
	printf("Used chips:     ");
	for (size_t curDev = 0; curDev < diList.size(); curDev ++)
	{
		const PLR_DEV_INFO& pdi = diList[curDev];
		const char* chipName;
		unsigned int drvCnt = 1;
		
		chipName = SndEmu_GetDevName(pdi.type, 0x01, pdi.devCfg);
		for (drvCnt = 1; curDev + 1 < diList.size(); curDev ++, drvCnt ++)
		{
			const PLR_DEV_INFO& pdi1 = diList[curDev + 1];
			const char* chipName1 = SndEmu_GetDevName(pdi1.type, 0x01, pdi1.devCfg);
			bool sameChip = (chipName == chipName1);	// we assume static pointers to chip names here
			sameChip &= (! (pdi.core != pdi1.core && genOpts.showDevCore));
			if (! sameChip)
				break;
		}
		if (pdi.type == DEVID_SN76496)
		{
			if (pdi.devCfg->flags && drvCnt > 1)
				drvCnt /= 2;	// the T6W28 consists of two "half" chips in VGMs
		}
		if (drvCnt > 1)
			printf("%ux", drvCnt);
		if (genOpts.showDevCore)
			printf("%s (%s), ", chipName, FCC2Str(pdi.core).c_str());
		else
			printf("%s, ", chipName);
	}
	printf("\b\b \n\n");
	return;
}

static void ShowConsoleTitle(const std::string& fileName, const std::string& titleTag, const std::string& gameTag)
{
	std::string titleStr;
	
	// show "Song (Game) - VGM Player" as console title
	if (! titleTag.empty())
		titleStr = titleTag;
	else
		titleStr = GetFileTitle(fileName.c_str());
	
	if (! gameTag.empty())
		titleStr = titleStr + " (" + gameTag + ")";
	
	titleStr = titleStr + " - " + APP_NAME;
	
#ifdef WIN32
	int bufSize = MultiByteToWideChar(CP_UTF8, 0, titleStr.c_str(), titleStr.size(), NULL, 0);
	std::wstring titleWStr(bufSize, '\0');
	MultiByteToWideChar(CP_UTF8, 0, titleStr.c_str(), titleStr.size(), &titleWStr[0], bufSize);
	
	SetConsoleTitleW(titleWStr.c_str());		// Set Windows Console Title
#else
	printf("\x1B]0;%s\x07", titleStr.c_str());	// Set xterm/rxvt Terminal Title
#endif
	
	return;
}

static UINT8 PlayFile(void)
{
	UINT8 retVal;
	bool needRefresh;
	
#if 0
	{
		PLR_SONG_INFO sInf;
		std::vector<PLR_DEV_INFO> diList;
		size_t curDev;
		
		player->GetSongInfo(sInf);
		player->GetSongDeviceInfo(diList);
		printf("SongInfo: %s v%X.%X, Rate %u/%u, Len %u, Loop at %d, devices: %u\n",
			FCC2Str(sInf.format).c_str(), sInf.fileVerMaj, sInf.fileVerMin,
			sInf.tickRateMul, sInf.tickRateDiv, sInf.songLen, sInf.loopTick, sInf.deviceCnt);
		for (curDev = 0; curDev < diList.size(); curDev ++)
		{
			const PLR_DEV_INFO& pdi = diList[curDev];
			printf(" Dev %d: Type 0x%02X #%d, Core %s, Clock %u, Rate %u, Volume 0x%X\n",
				(int)pdi.id, pdi.type, (INT8)pdi.instance, FCC2Str(pdi.core).c_str(), pdi.devCfg->clock, pdi.smplRate, pdi.volume);
		}
	}
#endif
	
	if (adOut.data != NULL)
		retVal = AudioDrv_SetCallback(adOut.data, FillBuffer, &myPlayer);
	else
		retVal = 0xFF;
	manualRenderLoop = (retVal != AERR_OK);
#ifndef _WIN32
	changemode(1);
#endif
	controlVal = 0;
	playState &= ~PLAYSTATE_END;
	needRefresh = true;
	while(! (playState & PLAYSTATE_END))
	{
		if (! (playState & PLAYSTATE_PAUSE))
			needRefresh = true;	// always update when playing
		if (needRefresh)
		{
			const char* pState;
			
			if (playState & PLAYSTATE_PAUSE)
				pState = "Paused ";
			else if (myPlayer.GetState() & PLAYSTATE_END)
				pState = "Finish ";
			else if (myPlayer.GetState() & PLAYSTATE_FADE)
				pState = "Fading ";
			else
				pState = "Playing";
			printf("%s%6.2f%%  %s / %s seconds  \r", pState,
					100.0 * myPlayer.GetCurPos(PLAYPOS_FILEOFS) / fileSize,
					GetTimeStr(myPlayer.GetCurTime(0), timeDispMode).c_str(),
					GetTimeStr(myPlayer.GetTotalTime(0), timeDispMode).c_str());
			fflush(stdout);
			needRefresh = false;
		}
		
		if (manualRenderLoop && ! (playState & PLAYSTATE_PAUSE))
		{
			UINT32 wrtBytes = FillBuffer(NULL, &myPlayer, audioBuf.size(), &audioBuf[0]);
			if (adOut.data != NULL)
				AudioDrv_WriteData(adOut.data, wrtBytes, &audioBuf[0]);
			else if (adLog.data != NULL)
				AudioDrv_WriteData(adLog.data, wrtBytes, &audioBuf[0]);
		}
		else
		{
			Sleep(50);
		}
		
		retVal = HandleKeyPress();
		if (retVal)
		{
			needRefresh = true;
			if (retVal >= 0x10)
				break;
		}
		
		if (genOpts.fadeRawLogs && isRawLog && genOpts.fadeTime_single > 0)
		{
			if (! (playState & PLAYSTATE_PAUSE) && ! (myPlayer.GetState() & PLAYSTATE_FADE))
			{
				double fadeStart = myPlayer.GetTotalTime(1) - genOpts.fadeTime_single / 1500.0;
				if (myPlayer.GetCurTime(1) >= fadeStart)
				{
					myPlayer.SetFadeSamples(MSec2Samples(genOpts.fadeTime_single, myPlayer));
					myPlayer.FadeOut();	// (FadeTime / 1500) ends at 33%
				}
			}
		}
	}
#ifndef _WIN32
	changemode(0);
#endif
	// remove callback to prevent further rendering
	// also waits for render thread to finish its work
	if (adOut.data != NULL)
	{
		if (dummyRenderAtLoad)
			AudioDrv_SetCallback(adOut.data, FillBufferDummy, NULL);
		else
			AudioDrv_SetCallback(adOut.data, NULL, NULL);
	}
	
	if (! controlVal)
	{
		if (/*stopAfterSong*/false)
			controlVal = 9;	// quit
		else
			controlVal = +1;	// finished normally - next song
	}
	printf("\n");
	
	return 0x00;
}

#ifdef WIN32
static int GetPressedKey(void)
{
	int keyCode = _getch();
	switch(keyCode)
	{
	case 0xE0:	// Special Key #1
		// Windows cursor key codes
		// Shift + Cursor results in the same code as if Shift wasn't pressed
		//	Key     None    Ctrl    Alt
		//	Up      E0 48   E0 8D   00 98
		//	Down    E0 50   E0 91   00 A0
		//	Left    E0 4B   E0 73   00 9B
		//	Right   E0 4D   E0 74   00 9D
		keyCode = _getch();	// Get 2nd Key
		switch(keyCode)
		{
		case 0x48:
			return KEY_UP;
		case 0x49:
			return KEY_PPAGE;
		case 0x4B:
			return KEY_LEFT;
		case 0x4D:
			return KEY_RIGHT;
		case 0x50:
			return KEY_DOWN;
		case 0x51:
			return KEY_NPAGE;
		case 0x73:
			return KEY_CTRL | KEY_LEFT;
		case 0x74:
			return KEY_CTRL | KEY_RIGHT;
		case 0x8D:
			return KEY_CTRL | KEY_UP;
		case 0x91:
			return KEY_CTRL | KEY_DOWN;
		}
		break;
	case 0x00:	// Special Key #2
		keyCode = _getch();	// Get 2nd Key
		switch(keyCode)
		{
		case 0x98:
			return KEY_ALT | KEY_UP;
		case 0x9B:
			return KEY_ALT | KEY_LEFT;
		case 0x9D:
			return KEY_ALT | KEY_RIGHT;
		case 0xA0:
			return KEY_ALT | KEY_DOWN;
		}
		break;
	}
	return keyCode;
}
#else
static int GetPressedKey(void)
{
	int keyCode = _getch();
	if (keyCode != 0x1B)
		return keyCode;
	
	int keyModifers = 0x00;
	
	// handle escape codes
	keyCode = _getch();
	if (keyCode == 0x1B || keyCode == 0x00)
		return 0x1B;	// ESC Key pressed
	switch(keyCode)
	{
	case 0x5B:
		// cursor only:     1B 41..44
		// Shift + cursor:  1B 31 3B 32 41..44
		// Alt + cursor:    1B 31 3B 33 41..44
		// Ctrl + cursor:   1B 31 3B 35 41..44
		// page down/up:    1B 35/36 7E
		keyCode = _getch();	// get 2nd Key
		if (keyCode == 0x31)	// Ctrl or Alt key
		{
			keyCode = _getch();
			if (keyCode == 0x3B)
			{
				keyCode = _getch();
				if ((keyCode - 0x31) & 0x01)
					keyModifers |= KEY_SHIFT;
				if ((keyCode - 0x31) & 0x02)
					keyModifers |= KEY_ALT;
				if ((keyCode - 0x31) & 0x04)
					keyModifers |= KEY_CTRL;
				keyCode = _getch();
			}
		}
		switch(keyCode)
		{
		case 0x35:
			_getch();	// skip 7E key code
			return KEY_PPAGE;
		case 0x36:
			_getch();	// skip 7E key code
			return KEY_NPAGE;
		case 0x41:
			return keyModifers | KEY_UP;
		case 0x42:
			return keyModifers | KEY_DOWN;
		case 0x43:
			return keyModifers | KEY_RIGHT;
		case 0x44:
			return keyModifers | KEY_LEFT;
		}
	}
	return keyCode;
}
#endif

static UINT8 HandleKeyPress(void)
{
	if (! _kbhit())
		return 0;
	
	int keyCode = GetPressedKey();
	if (keyCode >= 'a' && keyCode <= 'z')
		keyCode = toupper(keyCode);
	switch(keyCode)
	{
	case 0x1B:	// ESC
	case 'Q':	// quit
		playState |= PLAYSTATE_END;
		controlVal = +9;
		return 0x10;
	case ' ':
	case 'P':	// pause
		playState ^= PLAYSTATE_PAUSE;
		/*if (genOpts.soundWhilePaused)
		{
			// TODO
		}
		else*/ if (adOut.data != NULL)
		{
			if (playState & PLAYSTATE_PAUSE)
				AudioDrv_Pause(adOut.data);
			else
				AudioDrv_Resume(adOut.data);
		}
		break;
	case 'F':	// fade out
		// enforce "non-playlist" fade-out
		myPlayer.SetFadeSamples(MSec2Samples(genOpts.fadeTime_single, myPlayer));
		myPlayer.FadeOut();
		break;
	case 'R':	// restart
		OSMutex_Lock(renderMtx);
		myPlayer.Reset();
		OSMutex_Unlock(renderMtx);
		break;
	case KEY_LEFT:
	case KEY_RIGHT:
	case KEY_CTRL | KEY_LEFT:
	case KEY_CTRL | KEY_RIGHT:
		{
			UINT32 destPos;
			INT32 seekSmpls;
			
			INT32 secs = (keyCode & KEY_CTRL) ? 60 : 5;	// 5s [not ctrl] or 60s [ctrl]
			if ((keyCode & KEY_MASK) == KEY_LEFT)
				secs *= -1;	// seek back
			
			OSMutex_Lock(renderMtx);
			destPos = myPlayer.GetCurPos(PLAYPOS_SAMPLE);
			seekSmpls = myPlayer.GetSampleRate() * secs;
			if (seekSmpls < 0 && (UINT32)-seekSmpls > destPos)
				destPos = 0;
			else
				destPos += seekSmpls;
			myPlayer.Seek(PLAYPOS_SAMPLE, destPos);
			OSMutex_Unlock(renderMtx);
		}
		break;
	case 'B':	// previous file (back)
	case KEY_PPAGE:
		if (curSong > 0)
		{
			playState |= PLAYSTATE_END;
			controlVal = -1;
			return 0x10;
		}
		break;
	case 'N':	// next file
	case KEY_NPAGE:
		if (curSong + 1 < songList.size())
		{
			playState |= PLAYSTATE_END;
			controlVal = +1;
			return 0x10;
		}
		break;
	default:
		if (keyCode >= '0' && keyCode <= '9')
		{
			UINT32 maxPos;
			UINT8 pbPos10;
			UINT32 destPos;
			
			OSMutex_Lock(renderMtx);
			maxPos = myPlayer.GetPlayer()->GetTotalPlayTicks(genOpts.maxLoops);
			pbPos10 = keyCode - '0';
			destPos = maxPos * pbPos10 / 10;
			myPlayer.Seek(PLAYPOS_TICK, destPos);
			OSMutex_Unlock(renderMtx);
		}
		break;
	}
	
	return 0x01;
}

static inline std::string FCC2Str(UINT32 fcc)
{
	std::string result(4, '\0');
	result[0] = (char)((fcc >> 24) & 0xFF);
	result[1] = (char)((fcc >> 16) & 0xFF);
	result[2] = (char)((fcc >>  8) & 0xFF);
	result[3] = (char)((fcc >>  0) & 0xFF);
	return result;
}

static INT8 GetTimeDispMode(double seconds)
{
	UINT32 csec;
	UINT32 sec;
	UINT32 min;
	
	csec = (UINT32)(seconds * 100 + 0.5);
	sec = csec / 100;
	min = sec / 60;
	return (min >= 60);
}

static std::string GetTimeStr(double seconds, INT8 showHours)
{
	// showHours:
	//	-1 - auto
	//	 0 - show mm:ss
	//	 1 - show h:mm:ss
	UINT32 csec;
	UINT32 sec;
	UINT32 min;
	UINT32 hrs;
	char timeStr[0x20];
	
	csec = (UINT32)(seconds * 100 + 0.5);
	sec = csec / 100;
	csec %= 100;
	min = sec / 60;
	sec %= 60;
	if (showHours < 0)
		showHours = (min >= 60);
	if (showHours == 0)
	{
		sprintf(timeStr, "%02u:%02u.%02u", min, sec, csec);
	}
	else
	{
		showHours ++;
		hrs = min / 60;
		min %= 60;
		sprintf(timeStr, "%0*u:%02u:%02u.%02u", showHours, hrs, min, sec, csec);
	}
	
	return std::string(timeStr);
}

static UINT32 FillBuffer(void* drvStruct, void* userParam, UINT32 bufSize, void* data)
{
	PlayerWrapper* myPlr = (PlayerWrapper*)userParam;
	if (! (myPlr->GetState() & PLAYSTATE_PLAY))
	{
		fprintf(stderr, "Player Warning: calling Render while not playing! playState = 0x%02X\n", myPlr->GetState());
		memset(data, 0x00, bufSize);
		return bufSize;
	}
	
	UINT32 renderedBytes;
	OSMutex_Lock(renderMtx);
	renderedBytes = myPlr->Render(bufSize, data);
	OSMutex_Unlock(renderMtx);
	
	return renderedBytes;
}

static UINT32 FillBufferDummy(void* drvStruct, void* userParam, UINT32 bufSize, void* data)
{
	memset(data, 0x00, bufSize);
	return bufSize;
}

static UINT8 FilePlayCallback(PlayerBase* player, void* userParam, UINT8 evtType, void* evtParam)
{
	switch(evtType)
	{
	case PLREVT_START:
		//printf("Playback started.\n");
		break;
	case PLREVT_STOP:
		//printf("Playback stopped.\n");
		break;
	case PLREVT_LOOP:
		if (myPlayer.GetState() & PLAYSTATE_SEEK)
			break;
		//printf("Loop %u.\n", 1 + *(UINT32*)evtParam);
		break;
	case PLREVT_END:
		playState |= PLAYSTATE_END;
		//printf("Song End.\n");
		break;
	}
	return 0x00;
}

static DATA_LOADER* PlayerFileReqCallback(void* userParam, PlayerBase* player, const char* fileName)
{
	std::string filePath = FindFile_Single(fileName, appSearchPaths);
	if (filePath.empty())
	{
		printf("Unable to find %s!\n", fileName);
		return NULL;
	}
	//printf("Player requested file - found at %s\n", filePath.c_str());

	DATA_LOADER* dLoad = FileLoader_Init(filePath.c_str());
	UINT8 retVal = DataLoader_Load(dLoad);
	if (! retVal)
		return dLoad;
	DataLoader_Deinit(dLoad);
	return NULL;
}

static UINT8 ChooseAudioDriver(AudioDriver* aDrv)
{
	// special numbers for aDrv->dTypeID:
	//	-1 - select driver by name
	//	-2 - select last found driver
	UINT32 drvCount;
	UINT32 curDrv;
	AUDDRV_INFO* drvInfo;
	
	aDrv->driverID = -1;
	if (aDrv->dTypeID != -1)
	{
		INT32 typedDrv;
		UINT32 lastDrv;
		// go through all audio drivers get the ID of the requested Output/Disk Writer driver
		drvCount = Audio_GetDriverCount();
		lastDrv = (UINT32)-1;
		for (typedDrv = 0, curDrv = 0; curDrv < drvCount; curDrv ++)
		{
			Audio_GetDriverInfo(curDrv, &drvInfo);
			if (drvInfo->drvType == aDrv->driverType)
			{
				lastDrv = curDrv;
				if (typedDrv == aDrv->dTypeID)	// choose Nth driver with the respective type
				{
					aDrv->driverID = curDrv;
					break;
				}
				typedDrv ++;
			}
		}
		if (aDrv->dTypeID == -2)
			aDrv->driverID = lastDrv;
	}
	else
	{
		if (aDrv->dTypeName.empty())
			return 0x01;	// no driver chosen
		
		// go through all audio drivers get the ID of the requested Output/Disk Writer driver
		drvCount = Audio_GetDriverCount();
		for (curDrv = 0; curDrv < drvCount; curDrv ++)
		{
			Audio_GetDriverInfo(curDrv, &drvInfo);
			if (drvInfo->drvType == aDrv->driverType)
			{
				if (drvInfo->drvName == aDrv->dTypeName)	// choose driver with the requested name
				{
					aDrv->driverID = curDrv;
					break;
				}
			}
		}
	}
	
	return (aDrv->driverID == -1) ? 0xFF : 0x00;
}

static UINT8 InitAudioDriver(AudioDriver* aDrv)
{
	AUDDRV_INFO* drvInfo;
	UINT8 retVal;
	
	aDrv->data = NULL;
	retVal = AudioDrv_Init(aDrv->driverID, &aDrv->data);
	if (retVal)
		return retVal;
	
	Audio_GetDriverInfo(aDrv->driverID, &drvInfo);
#ifdef AUDDRV_DSOUND
	if (drvInfo->drvSig == ADRVSIG_DSOUND)
	{
		DSound_SetHWnd(AudioDrv_GetDrvData(aDrv->data), GetDesktopWindow());
		// We need to use a dummy audio renderer to prevent stuttering while loading files.
		dummyRenderAtLoad = true;
	}
#endif
#ifdef AUDDRV_PULSE
	if (drvInfo->drvSig == ADRVSIG_PULSE)
		Pulse_SetStreamDesc(AudioDrv_GetDrvData(aDrv->data), APP_NAME);
#endif
	
	return AERR_OK;
}

// initialize audio system and search for requested audio drivers
static UINT8 InitAudioSystem(void)
{
	AUDDRV_INFO* drvInfo;
	UINT8 retVal;
	UINT8 drvEnable;
	
	// mode 0: output to speakers (mask 01)
	// mode 1: write to file (mask 02)
	// mode 2: do both (mask 03)
	if (genOpts.pbMode <= 2)
		drvEnable = 0x01 + genOpts.pbMode;
	else
		drvEnable = 0x00;
	
	fprintf(stderr, "Opening Audio Device ...  ");
	fflush(stderr);
	retVal = Audio_Init();
	if (retVal == AERR_NODRVS)
		return retVal;
	
	// --- setup output to speakers ---
	if (drvEnable & 0x01)
	{
		adOut.dTypeID = (INT32)genOpts.audDriverID;
		adOut.dTypeName = genOpts.audDriverName;
		adOut.deviceID = (INT32)genOpts.audOutDev;
		if (adOut.dTypeID == -1 && adOut.dTypeName.empty())
		{
#ifdef _WIN32
			adOut.dTypeID = 0;	// on Windows, fall back to first driver (usually WinMM)
#else
			adOut.dTypeID = -2;	// on Linux, fall back to last driver (usually PulseAudio)
#endif
		}
	}
	else
	{
		adOut.dTypeID = -1;
		adOut.dTypeName = "";
	}
	retVal = ChooseAudioDriver(&adOut);
	if (retVal & 0x80)
	{
		fprintf(stderr, "Requested audio output driver not found!\n");
		Audio_Deinit();
		return AERR_NODRVS;
	}
	if (adOut.driverID != -1)
	{
		Audio_GetDriverInfo(adOut.driverID, &drvInfo);
		printf("Using driver %s.\n", drvInfo->drvName);
		retVal = InitAudioDriver(&adOut);
		if (retVal)
		{
			fprintf(stderr, "Audio Driver Init Error: %02X\n", retVal);
			Audio_Deinit();
			return retVal;
		}
	}
	
	// --- setup output to file ---
	if (drvEnable & 0x02)
	{
		adLog.dTypeID = 0;
		adLog.deviceID = 0;
	}
	else
	{
		adLog.dTypeID = -1;
		adLog.dTypeName = "";
	}
	retVal = ChooseAudioDriver(&adLog);
	if (retVal & 0x80)
	{
		fprintf(stderr, "Requested file writer driver not found!\n");
		if (adOut.data == NULL)	// cancel only when not playing back
		{
			Audio_Deinit();
			return AERR_NODRVS;
		}
	}
	if (adLog.driverID != -1)
	{
		Audio_GetDriverInfo(adLog.driverID, &drvInfo);
		fprintf(stderr, "Using file writer driver %s.\n", drvInfo->drvName);
		retVal = InitAudioDriver(&adLog);
		if (retVal)
		{
			fprintf(stderr, "Audio Driver Init Error: %02X\n", retVal);
			if (adOut.data == NULL)	// cancel only when not playing back
			{
				Audio_Deinit();
				return retVal;
			}
		}
	}

	retVal = OSMutex_Init(&renderMtx, 0);
	
	return AERR_OK;
}

static UINT8 DeinitAudioSystem(void)
{
	UINT8 retVal;
	
	retVal = 0x00;
	if (adLog.data != NULL)
	{
		AudioDrv_Deinit(&adLog.data);	adLog.data = NULL;
	}
	if (adOut.data != NULL)
	{
		retVal = AudioDrv_Deinit(&adOut.data);	adOut.data = NULL;
	}
	Audio_Deinit();
	
	OSMutex_Deinit(renderMtx);	renderMtx = NULL;
	
	return retVal;
}

static UINT8 StartAudioDevice(void)
{
	AUDIO_OPTS* opts;
	UINT8 retVal;
	UINT32 smplSize;
	UINT32 smplAlloc;
	UINT32 localBufSize;
	
	opts = NULL;
	if (adOut.data != NULL)
		opts = AudioDrv_GetOptions(adOut.data);
	else if (adLog.data != NULL)
		opts = AudioDrv_GetOptions(adLog.data);
	if (opts == NULL)
		return 0xFF;
	opts->sampleRate = genOpts.smplRate;
	opts->numChannels = 2;
	opts->numBitsPerSmpl = 16;
	if (genOpts.audBufTime)
		opts->usecPerBuf = genOpts.audBufTime * 1000;
	if (genOpts.audBufCnt)
		opts->numBuffers = genOpts.audBufCnt;
	smplSize = opts->numChannels * opts->numBitsPerSmpl / 8;
	smplAlloc = opts->sampleRate / 4;
	localBufSize = smplAlloc * smplSize;
	
	if (adOut.data != NULL)
	{
		printf("Opening Device %u ...\n", adOut.deviceID);
		retVal = AudioDrv_Start(adOut.data, adOut.deviceID);
		if (retVal)
		{
			fprintf(stderr, "Device Init Error: %02X\n", retVal);
			return retVal;
		}
		
		smplAlloc = AudioDrv_GetBufferSize(adOut.data) / smplSize;
		if (AudioDrv_SetCallback(adOut.data, NULL, NULL) == AERR_OK)
			localBufSize = 0;	// we don't need a local buffer when the audio driver itself comes with one
	}
	else if (adLog.data != NULL)
	{
		smplAlloc = opts->sampleRate / 4;
	}
	
	audioBuf.resize(localBufSize);
	myPlayer.PrepareRendering(opts, smplAlloc);
	
	return AERR_OK;
}

static UINT8 StopAudioDevice(void)
{
	UINT8 retVal;
	
	retVal = AERR_OK;
	if (adOut.data != NULL)
		retVal = AudioDrv_Stop(adOut.data);
	audioBuf.clear();
	
	return retVal;
}

static UINT8 StartDiskWriter(const std::string& songFileName)
{
	if (adLog.data == NULL)
		return 0x00;
	
	std::string outFName;
	const char* extPtr;
	UINT8 retVal;
	
	if (adOut.data != NULL)
	{
		AUDIO_OPTS* optsOut = AudioDrv_GetOptions(adOut.data);
		AUDIO_OPTS* optsLog = AudioDrv_GetOptions(adLog.data);
		*optsLog = *optsOut;
	}
	
	extPtr = GetFileExtension(songFileName.c_str());
	outFName = (extPtr != NULL) ? std::string(songFileName.c_str(), extPtr - 1) : songFileName;
	outFName += ".wav";
	
	//outFName = std::string("R:/") + GetFileTitle(outFName.c_str());
	
	WavWrt_SetFileName(AudioDrv_GetDrvData(adLog.data), outFName.c_str());
	retVal = AudioDrv_Start(adLog.data, 0);
	if (! retVal && adOut.data != NULL)
		AudioDrv_DataForward_Add(adOut.data, adLog.data);
	return retVal;
}

static UINT8 StopDiskWriter(void)
{
	if (adLog.data == NULL)
		return 0x00;
	
	if (adOut.data != NULL)
		AudioDrv_DataForward_Remove(adOut.data, adLog.data);
	return AudioDrv_Stop(adLog.data);
}


#ifdef WIN32
static void cls(void)
{
	// CLS-Function from the MSDN Help
	HANDLE hConsole;
	COORD coordScreen = {0, 0};
	BOOL bSuccess;
	DWORD cCharsWritten;
	CONSOLE_SCREEN_BUFFER_INFO csbi;
	DWORD dwConSize;
	
	hConsole = GetStdHandle(STD_OUTPUT_HANDLE);
	
	// get the number of character cells in the current buffer
	bSuccess = GetConsoleScreenBufferInfo(hConsole, &csbi);
	
	// fill the entire screen with blanks
	dwConSize = csbi.dwSize.X * csbi.dwSize.Y;
	bSuccess = FillConsoleOutputCharacter(hConsole, (TCHAR)' ', dwConSize, coordScreen,
											&cCharsWritten);
	
	// get the current text attribute
	//bSuccess = GetConsoleScreenBufferInfo(hConsole, &csbi);
	
	// now set the buffer's attributes accordingly
	//bSuccess = FillConsoleOutputAttribute(hConsole, csbi.wAttributes, dwConSize, coordScreen,
	//										&cCharsWritten);
	
	// put the cursor at (0, 0)
	bSuccess = SetConsoleCursorPosition(hConsole, coordScreen);
	
	return;
}
#endif

#ifndef _WIN32
static struct termios oldterm;
static UINT8 termmode = 0xFF;

static void changemode(UINT8 noEcho)
{
	if (termmode == 0xFF)
	{
		tcgetattr(STDIN_FILENO, &oldterm);
		termmode = 0;
	}
	if (termmode == noEcho)
		return;
	
	if (noEcho)
	{
		struct termios newterm;
		newterm = oldterm;
		newterm.c_lflag &= ~(ICANON | ECHO);
		tcsetattr(STDIN_FILENO, TCSANOW, &newterm);
		termmode = 1;
	}
	else
	{
		tcsetattr(STDIN_FILENO, TCSANOW, &oldterm);
		termmode = 0;
	}
	
	return;
}

static int _kbhit(void)
{
	struct timeval tv;
	fd_set rdfs;
	
	tv.tv_sec = 0;
	tv.tv_usec = 0;
	
	FD_ZERO(&rdfs);
	FD_SET(STDIN_FILENO, &rdfs);
	select(STDIN_FILENO + 1, &rdfs, NULL, NULL, &tv);
	
	return FD_ISSET(STDIN_FILENO, &rdfs);;
}

static void cls(void)
{
	system("clear");
	
	return;
}
#endif
