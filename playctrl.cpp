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
#if _WIN32_WINNT >= 0x0603
#include <wrl\wrappers\corewrappers.h>
#endif

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
#include <utils/DataLoader.h>
#include <utils/FileLoader.h>
#include <player/playerbase.hpp>
#include <player/s98player.hpp>
#include <player/droplayer.hpp>
#include <player/vgmplayer.hpp>
#include <player/playera.hpp>
#include <audio/AudioStream.h>
#include <audio/AudioStream_SpcDrvFuns.h>
#include <utils/OSMutex.h>
#include <utils/StrUtils.h>

#include "utils.hpp"
#include "config.hpp"
#include "m3uargparse.hpp"
#include "playcfg.hpp"
#include "mediainfo.hpp"
#include "version.h"
#include "mediactrl.hpp"


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
static bool AdvanceSongList(size_t& songIdx, int controlVal);
static DATA_LOADER* GetFileLoaderUTF8(const std::string& fileName);
static UINT8 OpenFile(const std::string& fileName, DATA_LOADER*& dLoad, PlayerBase*& player);
static void PreparePlayback(void);
static void ShowSongInfo(void);
static void ShowConsoleTitle(void);
static UINT8 PlayFile(void);
static UINT8 HandleCtrlEvent(UINT8 evtType, INT32 evtParam);

static int GetPressedKey(void);
static UINT8 HandleKeyPress(bool waitForKey);
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


static AudioDriver adOut /*= {ADRVTYPE_OUT, -1, "", 0, 0, NULL}*/;
static AudioDriver adLog /*= {ADRVTYPE_DISK, -1, "", 0, 0, NULL}*/;

static std::vector<UINT8> audioBuf;
static OS_MUTEX* renderMtx;	// render thread mutex

#ifdef _WIN32
static CPCONV* cpcU8_Wide;
#if ! HAVE_FILELOADER_W
static CPCONV* cpcU8_ACP;
#endif
#endif

static bool manualRenderLoop = false;
static bool dummyRenderAtLoad = false;

static INT8 timeDispMode = 0;

extern std::vector<std::string> appSearchPaths;
extern Configuration playerCfg;
extern std::vector<SongFileList> songList;
extern std::vector<PlaylistFileList> plList;

static int controlVal;
static size_t curSong;

static MediaInfo mediaInfo;
static MediaControl mediaCtrl;

static inline UINT32 MSec2Samples(UINT32 val, const PlayerA& player)
{
	return (UINT32)(((UINT64)val * player.GetSampleRate() + 500) / 1000);
}

UINT8 PlayerMain(UINT8 showFileName)
{
	PlayerA& myPlayer = mediaInfo._player;
	GeneralOptions& genOpts = mediaInfo._genOpts;
	UINT8 retVal;
	UINT8 fnShowMode;
	
#if _WIN32_WINNT >= 0x0603
	// The Windows Runtime (with COM) MUST be initialized before the audio API,
	// which may initialize the COM library by itself and defaults to single-thread mode.
	Microsoft::WRL::Wrappers::RoInitializeWrapper initialize(RO_INIT_MULTITHREADED);
#endif
	
	if (songList.size() == 1 && songList[0].playlistID == (size_t)-1)
		fnShowMode = showFileName ? 1 : 2;
	else
		fnShowMode = 0;
	
	ParseConfiguration(genOpts, 0x100, mediaInfo._chipOpts, playerCfg);
	
	{
		// Manual initialization of adOut/adLog, because MSVC6 is unable to
		// use initializer lists with classes that have (non-default) constructors.
		adOut.driverType = ADRVTYPE_OUT;
		adOut.dTypeID = -1;
		adOut.dTypeName = "";
		adOut.driverID = 0;
		adOut.deviceID = 0;
		adOut.data = NULL;
		adLog = adOut;
		adLog.driverType = ADRVTYPE_DISK;
	}
	
	retVal = InitAudioSystem();
	if (retVal)
		return 1;
	retVal = StartAudioDevice();
	if (retVal)
	{
		DeinitAudioSystem();
		return 1;
	}
	mediaInfo._playState = 0x00;
	
#ifdef _WIN32
	retVal = CPConv_Init(&cpcU8_Wide, "UTF-8", "UTF-16LE");
#if ! HAVE_FILELOADER_W
	{
		std::string cpName(0x10, '\0');
		snprintf(&cpName[0], cpName.size(), "CP%u", GetACP());
		retVal = CPConv_Init(&cpcU8_ACP, "UTF-8", cpName.c_str());
	}
#endif
#endif
	
	// I'll keep the instances of the players for the program's life time.
	// This way player/chip options are kept between track changes.
	myPlayer.RegisterPlayerEngine(new VGMPlayer);
	myPlayer.RegisterPlayerEngine(new S98Player);
	myPlayer.RegisterPlayerEngine(new DROPlayer);
	myPlayer.SetEventCallback(FilePlayCallback, NULL);
	myPlayer.SetFileReqCallback(PlayerFileReqCallback, NULL);
	ApplyCfg_General(myPlayer, genOpts);
	for (size_t curChp = 0; curChp < 0x100; curChp ++)
	{
		const ChipOptions& cOpt = mediaInfo._chipOpts[curChp];
		if (cOpt.chipType == 0xFF)
			continue;
		ApplyCfg_Chip(myPlayer, genOpts, cOpt);
	}
	mediaInfo._pbSongCnt = songList.size();
	
	mediaInfo._enableAlbumImage = false;	// disable by default, MediaCtrl objects will enable it on demand
	//mediaInfo.AddSignalCallback(SignalCB, NULL);
	mediaCtrl.Init(mediaInfo);
	
#ifndef _WIN32
	changemode(1);
#endif
	//resVal = 0;
	controlVal = +1;	// default: next song
	for (curSong = 0; curSong < songList.size(); )
	{
		const SongFileList& sfl = songList[curSong];
		DATA_LOADER* dLoad;
		PlayerBase* player;
		
		mediaInfo._pbSongID = curSong;
		mediaInfo._songPath = sfl.fileName;
		mediaInfo._playlistTrkID = sfl.playlistSongID;
		if (sfl.playlistSongID == (size_t)-1)
		{
			mediaInfo._playlistPath = std::string();
			mediaInfo._playlistTrkCnt = 0;
		}
		else
		{
			const PlaylistFileList& pfl = plList[sfl.playlistID];
			mediaInfo._playlistPath = pfl.fileName;
			mediaInfo._playlistTrkCnt = pfl.songCount;
		}
		
		if (fnShowMode == 1)
		{
			u8printf("File Name:      %s\n", mediaInfo._songPath.c_str());
		}
		else if (fnShowMode == 0)
		{
			cls();
			printf(APP_NAME);
			printf("\n----------\n");
			if (mediaInfo._playlistTrkID == (size_t)-1)
			{
				printf("\n");
			}
			else
			{
				u8printf("Playlist File:  %s\n", mediaInfo._playlistPath.c_str());
				//printf("Playlist File:  %s [song %u/%u]\n", mediaInfo._playlistPath.c_str(),
				//	1 + (unsigned)mediaInfo._playlistTrkID, (unsigned)mediaInfo._playlistTrkCnt);
			}
			u8printf("File Name:      [%*u/%u] %s\n", count_digits((int)mediaInfo._pbSongCnt), 1 + (unsigned)mediaInfo._pbSongID,
				(unsigned)mediaInfo._pbSongCnt, mediaInfo._songPath.c_str());
		}
		fflush(stdout);
		
		retVal = OpenFile(sfl.fileName, dLoad, player);
		if (retVal & 0x80)
		{
			if (curSong == 0 && controlVal < 0)
				controlVal = +1;
			HandleKeyPress(true);
			if (! mediaInfo._evtQueue.empty())
			{
				MediaInfo::EventData ed = mediaInfo._evtQueue.front();
				mediaInfo._evtQueue.pop();
				retVal = HandleCtrlEvent(ed.evt, ed.value);
			}
			if (! AdvanceSongList(curSong, controlVal))
				break;
			else
				continue;
		}
		printf("\n");
		
		mediaInfo._fileEndPos = myPlayer.GetFileSize();
		mediaInfo.PreparePlayback();
		PreparePlayback();
		mediaInfo.SearchAlbumImage();
		
		// call "start" before showing song info, so that we can get the sound cores
		myPlayer.Start();
		mediaInfo._playState |= PLAYSTATE_PLAY;	// tell the key handler to enable playback controls
		
		mediaInfo.EnumerateChips();
		myPlayer.Render(0, NULL);	// process first sample
		mediaInfo._fileStartPos = myPlayer.GetCurPos(PLAYPOS_FILEOFS);	// get position after processing initialization block
		timeDispMode = GetTimeDispMode(myPlayer.GetTotalTime(0));
		if (genOpts.setTermTitle)
			ShowConsoleTitle();
		ShowSongInfo();
		
		retVal = StartDiskWriter(sfl.fileName);
		if (retVal)
			fprintf(stderr, "Warning: File writer failed with error 0x%02X\n", retVal);
		
		mediaInfo.Signal(MI_SIG_NEW_SONG);
		PlayFile();
		StopDiskWriter();
		
		mediaInfo._playState &= ~PLAYSTATE_PLAY;
		myPlayer.Stop();
		mediaInfo.Signal(MI_SIG_PLAY_STATE);
		
		myPlayer.UnloadFile();
		DataLoader_Deinit(dLoad);
		
		if (! AdvanceSongList(curSong, controlVal))
			break;
	}	// end for(curSong)
#ifndef _WIN32
	changemode(0);
#endif
	mediaCtrl.Deinit();
	
	myPlayer.UnregisterAllPlayers();
	
#ifdef _WIN32
	CPConv_Deinit(cpcU8_Wide);
#if ! HAVE_FILELOADER_W
	CPConv_Deinit(cpcU8_ACP);
#endif
#endif
	
	StopAudioDevice();
	DeinitAudioSystem();
	
	return 0;
}

static bool AdvanceSongList(size_t& songIdx, int controlVal)
{
	if (controlVal == +9)
		return false;	// "break" value
	
	if (controlVal > 0)
	{
		songIdx ++;
	}
	else if (controlVal < 0)
	{
		if (songIdx > 0)
			songIdx --;
	}
	return true;
}

static DATA_LOADER* GetFileLoaderUTF8(const std::string& fileNameU8)
{
#ifndef _WIN32
	return FileLoader_Init(fileNameU8.c_str());
#else
#if HAVE_FILELOADER_W
	size_t fileNameWLen = 0;
	wchar_t* fileNameWStr = NULL;
	UINT8 retVal = CPConv_StrConvert(cpcU8_Wide, &fileNameWLen, reinterpret_cast<char**>(&fileNameWStr),
		fileNameU8.length() + 1, fileNameU8.c_str());	// length()+1 to include the \0
	DATA_LOADER* dLoad = NULL;
	if (retVal < 0x80)
		dLoad = FileLoader_InitW(fileNameWStr);
	free(fileNameWStr);
	return dLoad;
#else
	size_t fileNameALen = 0;
	char* fileNameAStr = NULL;
	UINT8 retVal = CPConv_StrConvert(cpcU8_ACP, &fileNameALen, &fileNameAStr,
		fileNameU8.length() + 1, fileNameU8.c_str());	// length()+1 to include the \0
	DATA_LOADER* dLoad = NULL;
	if (retVal < 0x80)
		dLoad = FileLoader_Init(fileNameAStr);
	free(fileNameAStr);
	return dLoad;
#endif
#endif
}

static UINT8 OpenFile(const std::string& fileName, DATA_LOADER*& dLoad, PlayerBase*& player)
{
	UINT8 retVal;
	
	dLoad = GetFileLoaderUTF8(fileName);
	if (dLoad == NULL)
		return 0xFF;
	DataLoader_SetPreloadBytes(dLoad, 0x100);
	retVal = DataLoader_Load(dLoad);
	if (retVal)
	{
		DataLoader_CancelLoading(dLoad);
		DataLoader_Deinit(dLoad);
		fprintf(stderr, "Error 0x%02X opening file!\n", retVal);
		return 0xFF;
	}
	retVal = mediaInfo._player.LoadFile(dLoad);
	if (retVal)
	{
		DataLoader_CancelLoading(dLoad);
		DataLoader_Deinit(dLoad);
		fprintf(stderr, "Unknown file format! (Error 0x%02X)\n", retVal);
		return 0xFF;
	}
	return 0x00;
}

static void PreparePlayback(void)
{
	PlayerA& myPlayer = mediaInfo._player;
	const GeneralOptions& genOpts = mediaInfo._genOpts;
	UINT32 timeMS;
	
	if (myPlayer.GetPlayer()->GetPlayerType() == FCC_VGM)
	{
		VGMPlayer* vgmplay = dynamic_cast<VGMPlayer*>(myPlayer.GetPlayer());
		myPlayer.SetLoopCount(vgmplay->GetModifiedLoopCount(genOpts.maxLoops));
	}
	
	// last song: fadeTime_single, others: fadeTime_plist
	timeMS = (curSong + 1 == songList.size()) ? genOpts.fadeTime_single : genOpts.fadeTime_plist;
	myPlayer.SetFadeSamples(MSec2Samples(timeMS, myPlayer));
	
	timeMS = (myPlayer.GetPlayer()->GetLoopTicks() == 0) ? genOpts.pauseTime_jingle : genOpts.pauseTime_loop;
	myPlayer.SetEndSilenceSamples(MSec2Samples(timeMS, myPlayer));
	
	return;
}

static void ShowSongInfo(void)
{
	PlayerBase* player = mediaInfo._player.GetPlayer();
	
	u8printf("Track Title:    %s\n", mediaInfo.GetSongTagForDisp("TITLE"));
	u8printf("Game Name:      %s\n", mediaInfo.GetSongTagForDisp("GAME"));
	u8printf("System:         %s\n", mediaInfo.GetSongTagForDisp("SYSTEM"));
	u8printf("Composer:       %s\n", mediaInfo.GetSongTagForDisp("ARTIST"));
	if (player->GetPlayerType() == FCC_S98)
	{
		S98Player* s98play = dynamic_cast<S98Player*>(player);
		const S98_HEADER* s98hdr = s98play->GetFileHeader();
		
		u8printf("Release:        %-11s Tick Rate: %u/%u\n", mediaInfo.GetSongTagForDisp("DATE"),
			s98hdr->tickMult, s98hdr->tickDiv);
	}
	else
	{
		u8printf("Release:        %s\n", mediaInfo.GetSongTagForDisp("DATE"));
	}
	printf("Format:         %-11s ", mediaInfo._fileVerStr.c_str());
	printf("Gain:%5.2f    ", mediaInfo._volGain);
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
		if (mediaInfo._looping)
			printf("Loop: Yes (%s)\n", GetTimeStr(mediaInfo._player.GetLoopTime(), -1).c_str());
		else if (mediaInfo._isRawLog && mediaInfo._genOpts.fadeRawLogs)
			printf("Loop: No (raw)\n");
		else
			printf("Loop: No\n");
	}
	u8printf("VGM by:         %s\n", mediaInfo.GetSongTagForDisp("ENCODED_BY"));
	u8printf("Notes:          %s\n", mediaInfo.GetSongTagForDisp("COMMENT"));
	printf("\n");
	
	printf("Used chips:     ");
	for (size_t curDev = 0; curDev < mediaInfo._chipList.size(); curDev ++)
	{
		const MediaInfo::DeviceItem& di = mediaInfo._chipList[curDev];
		unsigned int devCnt = 1;
		
		for (; curDev + 1 < mediaInfo._chipList.size(); curDev ++)
		{
			const MediaInfo::DeviceItem& di1 = mediaInfo._chipList[curDev + 1];
			if (di1.name != di.name)
				break;
			if (mediaInfo._genOpts.showDevCore && di1.core != di.core)
				break;
			devCnt ++;
		}
		
		if (devCnt > 1)
			printf("%ux", devCnt);
		if (mediaInfo._genOpts.showDevCore)
			printf("%s (%s), ", di.name.c_str(), di.core.c_str());
		else
			printf("%s, ", di.name.c_str());
	}
	printf("\b\b \n\n");
	return;
}

static void ShowConsoleTitle(void)
{
	const std::string& fileName = mediaInfo._songPath;
	std::string titleTag = mediaInfo.GetSongTagForDisp("TITLE");
	std::string gameTag = mediaInfo.GetSongTagForDisp("GAME");
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
	size_t titleWLen = 0;
	wchar_t* titleWStr = NULL;
	UINT8 retVal = CPConv_StrConvert(cpcU8_Wide, &titleWLen, reinterpret_cast<char**>(&titleWStr),
		titleStr.length() + 1, titleStr.c_str());	// length()+1 to include the \0
	if (retVal < 0x80)
		SetConsoleTitleW(titleWStr);		// Set Windows Console Title
	free(titleWStr);
#else
	printf("\x1B]0;%s\x07", titleStr.c_str());	// Set xterm/rxvt Terminal Title
#endif
	
	return;
}

static UINT8 PlayFile(void)
{
	const GeneralOptions& genOpts = mediaInfo._genOpts;
	PlayerA& myPlayer = mediaInfo._player;
	UINT8 retVal;
	bool needRefresh;
	
	const std::vector<VGMPlayer::DACSTRM_DEV>* vgmPcmStrms = NULL;
	if (myPlayer.GetPlayer()->GetPlayerType() == FCC_VGM)
	{
		VGMPlayer* vgmplay = dynamic_cast<VGMPlayer*>(myPlayer.GetPlayer());
		vgmPcmStrms = &vgmplay->GetStreamDevInfo();
	}
	
	if (adOut.data != NULL)
		retVal = AudioDrv_SetCallback(adOut.data, FillBuffer, &myPlayer);
	else
		retVal = 0xFF;
	manualRenderLoop = (retVal != AERR_OK);
	controlVal = 0;
	mediaInfo._playState &= ~PLAYSTATE_END;
	needRefresh = true;
	while(! (mediaInfo._playState & PLAYSTATE_END))
	{
		if (! (mediaInfo._playState & PLAYSTATE_PAUSE))
			needRefresh = true;	// always update when playing
		if (needRefresh)
		{
			const char* pState;
			
			if (mediaInfo._playState & PLAYSTATE_PAUSE)
				pState = "Paused ";
			else if (myPlayer.GetState() & PLAYSTATE_END)
				pState = "Finish ";
			else if (myPlayer.GetState() & PLAYSTATE_FADE)
				pState = "Fading ";
			else
				pState = "Playing";
			
			UINT32 dataLen = mediaInfo._fileEndPos - mediaInfo._fileStartPos;
			UINT32 dataPos = myPlayer.GetCurPos(PLAYPOS_FILEOFS);
			dataPos = (dataPos >= mediaInfo._fileStartPos) ? (dataPos - mediaInfo._fileStartPos) : 0x00;
			
			if (vgmPcmStrms == NULL || vgmPcmStrms->empty())
			{
				printf("%s%6.2f%%  %s / %s seconds  \r", pState,
					100.0 * dataPos / dataLen,
					GetTimeStr(myPlayer.GetCurTime(0), timeDispMode).c_str(),
					GetTimeStr(myPlayer.GetTotalTime(0), timeDispMode).c_str());
			}
			else
			{
				const VGMPlayer::DACSTRM_DEV* strmDev = &(*vgmPcmStrms)[0];
				std::string pbMode;
				if (strmDev->pbMode & 0x10)
					pbMode += 'R';	// reverse playback
				if (strmDev->pbMode & 0x80)
					pbMode += 'L';	// looping
				printf("%s%6.2f%%  %s / %s seconds", pState,
					100.0 * dataPos / dataLen,
					GetTimeStr(myPlayer.GetCurTime(0), timeDispMode).c_str(),
					GetTimeStr(myPlayer.GetTotalTime(0), timeDispMode).c_str());
				if (genOpts.showStrmCmds == 0x01)
					printf("  %02X / %02X %s", 1 + strmDev->lastItem, strmDev->maxItems, pbMode.c_str());
				else if (genOpts.showStrmCmds == 0x02)
					printf("  %02X / %02X at %5u Hz %s", 1 + strmDev->lastItem, strmDev->maxItems,
						strmDev->freq, pbMode.c_str());
				else if (genOpts.showStrmCmds == 0x03)
					printf("  %02X / %02X at %4.1f KHz %s", 1 + strmDev->lastItem, strmDev->maxItems,
						strmDev->freq / 1000.0, pbMode.c_str());
				printf("  \r");
			}
			fflush(stdout);
			needRefresh = false;
		}
		
		if (manualRenderLoop && ! (mediaInfo._playState & PLAYSTATE_PAUSE))
		{
			UINT32 wrtBytes = FillBuffer(NULL, &myPlayer, (UINT32)audioBuf.size(), &audioBuf[0]);
			if (adOut.data != NULL)
				AudioDrv_WriteData(adOut.data, wrtBytes, &audioBuf[0]);
			else if (adLog.data != NULL)
				AudioDrv_WriteData(adLog.data, wrtBytes, &audioBuf[0]);
		}
		else
		{
			Sleep(50);
		}
		
		HandleKeyPress(false);
		retVal = 0x00;
		if (! mediaInfo._evtQueue.empty())	// TODO: thread-safety
		{
			MediaInfo::EventData ed = mediaInfo._evtQueue.front();
			mediaInfo._evtQueue.pop();
			retVal = HandleCtrlEvent(ed.evt, ed.value);
		}
		if (retVal)
		{
			needRefresh = true;
			if (retVal >= 0x10)
				break;
		}
		
		if (genOpts.fadeRawLogs && mediaInfo._isRawLog && genOpts.fadeTime_single > 0)
		{
			// TODO: Thread-safety
			if (! (mediaInfo._playState & PLAYSTATE_PAUSE) && ! (myPlayer.GetState() & PLAYSTATE_FADE))
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

static UINT8 HandleCtrlEvent(UINT8 evtType, INT32 evtParam)
{
	const GeneralOptions& genOpts = mediaInfo._genOpts;
	PlayerA& myPlayer = mediaInfo._player;
	
	switch(evtType)
	{
	case MI_EVT_PLIST:
		switch(evtParam)
		{
		case MIE_PL_QUIT:	// quit
			mediaInfo._playState |= PLAYSTATE_END;
			controlVal = +9;
			return 0x10;
		case MIE_PL_PREV:	// previous file
			if (curSong <= 0)
				break;
			mediaInfo._playState |= PLAYSTATE_END;
			controlVal = -1;
			return 0x10;
		case MIE_PL_NEXT:	// next file
			if (curSong + 1 >= songList.size())
				break;
			mediaInfo._playState |= PLAYSTATE_END;
			controlVal = +1;
			return 0x10;
		}
		break;
	case MI_EVT_CONTROL:
		switch(evtParam)
		{
		case MIE_CTRL_START:	// start
			if (! (mediaInfo._playState & PLAYSTATE_PLAY))
				break;
			OSMutex_Lock(renderMtx);
			mediaInfo._playState &= ~PLAYSTATE_PAUSE;
			OSMutex_Unlock(renderMtx);
			if (adOut.data != NULL)
				AudioDrv_Resume(adOut.data);
			mediaInfo.Signal(MI_SIG_PLAY_STATE);
			return 0x01;
		case MIE_CTRL_STOP:	// stop
			if (! (mediaInfo._playState & PLAYSTATE_PLAY))
				break;
			OSMutex_Lock(renderMtx);
			mediaInfo._playState |= PLAYSTATE_PAUSE;
			myPlayer.Reset();
			OSMutex_Unlock(renderMtx);
			if (adOut.data != NULL)
				AudioDrv_Pause(adOut.data);
			mediaInfo.Signal(MI_SIG_POSITION | MI_SIG_PLAY_STATE);
			return 0x01;
			//return 0x10;
		case MIE_CTRL_RESTART:	// restart
			if (! (mediaInfo._playState & PLAYSTATE_PLAY))
				break;
			OSMutex_Lock(renderMtx);
			myPlayer.Reset();
			OSMutex_Unlock(renderMtx);
			mediaInfo.Signal(MI_SIG_POSITION);
			return 0x01;
		}
		break;
	case MI_EVT_PAUSE:
		if (! (mediaInfo._playState & PLAYSTATE_PLAY))
			break;
		switch(evtParam)
		{
		case MIE_PS_PAUSE:	// pause
			mediaInfo._playState |= PLAYSTATE_PAUSE;
			break;
		case MIE_PS_RESUME:	// resume
			mediaInfo._playState &= ~PLAYSTATE_PAUSE;
			break;
		case MIE_PS_TOGGLE:	// pause toggle
			mediaInfo._playState ^= PLAYSTATE_PAUSE;
			break;
		}
		/*if (genOpts.soundWhilePaused)
		{
			// TODO
		}
		else*/ if (adOut.data != NULL)
		{
			if (mediaInfo._playState & PLAYSTATE_PAUSE)
				AudioDrv_Pause(adOut.data);
			else
				AudioDrv_Resume(adOut.data);
		}
		mediaInfo.Signal(MI_SIG_PLAY_STATE);
		return 0x01;
	case MI_EVT_FADE:	// fade out
		if (! (mediaInfo._playState & PLAYSTATE_PLAY))
			break;
		// enforce "non-playlist" fade-out
		OSMutex_Lock(renderMtx);
		myPlayer.SetFadeSamples(MSec2Samples(genOpts.fadeTime_single, myPlayer));
		myPlayer.FadeOut();
		OSMutex_Unlock(renderMtx);
		return 0x01;
	case MI_EVT_SEEK_REL:
		if (! (mediaInfo._playState & PLAYSTATE_PLAY))
			break;
		OSMutex_Lock(renderMtx);
		{
			UINT32 destPos = mediaInfo._player.GetCurPos(PLAYPOS_SAMPLE);
			if (evtParam < 0 && (UINT32)-evtParam > destPos)
				destPos = 0;
			else
				destPos += evtParam;
			mediaInfo._player.Seek(PLAYPOS_SAMPLE, destPos);
		}
		OSMutex_Unlock(renderMtx);
		mediaInfo.Signal(MI_SIG_POSITION);
		return 0x01;
	case MI_EVT_SEEK_ABS:
		if (! (mediaInfo._playState & PLAYSTATE_PLAY))
			break;
		OSMutex_Lock(renderMtx);
		mediaInfo._player.Seek(PLAYPOS_SAMPLE, (UINT32)evtParam);
		OSMutex_Unlock(renderMtx);
		mediaInfo.Signal(MI_SIG_POSITION);
		return 0x01;
	case MI_EVT_SEEK_PERC:
		if (! (mediaInfo._playState & PLAYSTATE_PLAY))
			break;
		if (evtParam < 0)
			evtParam = 0;
		{
			UINT32 maxPos;
			UINT32 destPos;
			
			OSMutex_Lock(renderMtx);
			maxPos = myPlayer.GetPlayer()->GetTotalPlayTicks(genOpts.maxLoops);
			destPos = maxPos * evtParam / 100;
			myPlayer.Seek(PLAYPOS_TICK, destPos);
			OSMutex_Unlock(renderMtx);
			mediaInfo.Signal(MI_SIG_POSITION);
		}
		return 0x01;
	}
	
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

static UINT8 HandleKeyPress(bool waitForKey)
{
	if (! waitForKey && ! _kbhit())
		return 0;
	
	int keyCode = GetPressedKey();
	if (keyCode >= 'a' && keyCode <= 'z')
		keyCode = toupper(keyCode);
	switch(keyCode)
	{
	case 0x1B:	// ESC
	case 'Q':	// quit
		mediaInfo.Event(MI_EVT_PLIST, MIE_PL_QUIT);
		break;
	case ' ':
	case 'P':	// pause
		mediaInfo.Event(MI_EVT_PAUSE, MIE_PS_TOGGLE);
		break;
	case 'F':	// fade out
		mediaInfo.Event(MI_EVT_FADE, 0);
		break;
	case 'R':	// restart
		mediaInfo.Event(MI_EVT_CONTROL, MIE_CTRL_RESTART);
		break;
	case KEY_LEFT:
	case KEY_RIGHT:
	case KEY_CTRL | KEY_LEFT:
	case KEY_CTRL | KEY_RIGHT:
		{
			INT32 secs = (keyCode & KEY_CTRL) ? 60 : 5;	// 5s [not ctrl] or 60s [ctrl]
			if ((keyCode & KEY_MASK) == KEY_LEFT)
				secs *= -1;	// seek back
			mediaInfo.Event(MI_EVT_SEEK_REL, mediaInfo._player.GetSampleRate() * secs);
		}
		break;
	case 'B':	// previous file (back)
	case KEY_PPAGE:
		mediaInfo.Event(MI_EVT_PLIST, MIE_PL_PREV);
		break;
	case 'N':	// next file
	case KEY_NPAGE:
		mediaInfo.Event(MI_EVT_PLIST, MIE_PL_NEXT);
		break;
	default:
		if (keyCode >= '0' && keyCode <= '9')
		{
			UINT8 pbPos10 = keyCode - '0';
			mediaInfo.Event(MI_EVT_SEEK_PERC, pbPos10 * 10);
		}
		break;
	}
	
	return 1;
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
	PlayerA* myPlr = (PlayerA*)userParam;
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
		if (mediaInfo._player.GetState() & PLAYSTATE_SEEK)
			break;
		//printf("Loop %u.\n", 1 + *(UINT32*)evtParam);
		mediaInfo.Signal(MI_SIG_POSITION);
		break;
	case PLREVT_END:
		mediaInfo._playState |= PLAYSTATE_END;
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
		fprintf(stderr, "Unable to find %s!\n", fileName);
		return NULL;
	}
	//fprintf(stderr, "Player requested file - found at %s\n", filePath.c_str());

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
	const GeneralOptions& genOpts = mediaInfo._genOpts;
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
	
	//fprintf(stderr, "Opening Audio Device ...  ");	fflush(stderr);
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
		//fprintf(stderr, "Using driver %s.\n", drvInfo->drvName);
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
		//fprintf(stderr, "Using file writer driver %s.\n", drvInfo->drvName);
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
	const GeneralOptions& genOpts = mediaInfo._genOpts;
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
		//fprintf(stderr, "Opening Device %u ...\n", adOut.deviceID);
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
	mediaInfo._player.SetOutputSettings(opts->sampleRate, opts->numChannels, opts->numBitsPerSmpl, smplAlloc);
	
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
