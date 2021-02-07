#ifndef __MEDIAINFO_HPP__
#define __MEDIAINFO_HPP__

#include <string>
#include <vector>
#include <map>
#include <queue>

#include <stdtype.h>
#include <player/playera.hpp>
#include "playcfg.hpp"

#define MI_SIG_NEW_SONG		0x01	// triggered when a new song starts (-> metadata refresh)
#define MI_SIG_PLAY_STATE	0x02	// playback status change
#define MI_SIG_POSITION		0x04	// playback position change (-> seeking)
#define MI_SIG_VOLUME		0x08	// volume change

#define MI_EVT_CONTROL		0x00	// playback control
	#define MIE_CTRL_START		0x01
	#define MIE_CTRL_STOP		0x02
	#define MIE_CTRL_RESTART	0x03
#define MI_EVT_PAUSE		0x01
	#define MIE_PS_PAUSE		0x01
	#define MIE_PS_RESUME		0x02
	#define MIE_PS_TOGGLE		0x03	// pause toggle
#define MI_EVT_FADE			0x02	// fade out
#define MI_EVT_PLIST		0x03	// playlist control: +1 - next, -1 - previous, +9 - stop
	#define MIE_PL_NEXT			+1
	#define MIE_PL_PREV			-1
	#define MIE_PL_QUIT			9
#define MI_EVT_SEEK_REL		0x10	// relative seeking (in samples)
#define MI_EVT_SEEK_ABS		0x11	// absolute seeking (in samples)
#define MI_EVT_SEEK_PERC	0x12	// absolute seeking (in percent)

class MediaInfo;
typedef void (*MI_SIGNAL_CB)(MediaInfo* mInfo, void* userParam, UINT8 signalMask);

class MediaInfo
{
public:
	void PreparePlayback(void);
	const char* GetSongTagForDisp(const std::string& tagName);
	void EnumerateTags(void);	// implicitly called by PreparePlayback(), as that one may parse some of the tags
	void EnumerateChips(void);	// must be called after starting playback in order to retrieve used sound core IDs
	void SearchAlbumImage(void);
	
	void AddSignalCallback(MI_SIGNAL_CB func, void* param);
	void Event(UINT8 evtType, INT32 evtParam);
	void Signal(UINT8 signalMask);
	
	struct DeviceItem
	{
		std::string name;
		std::string core;
	};
	struct SignalHandler
	{
		MI_SIGNAL_CB func;
		void* param;
	};
	struct EventData
	{
		UINT8 evt;
		INT32 value;
	};
	
	volatile UINT8 _playState;
	GeneralOptions _genOpts;
	ChipOptions _chipOpts[0x100];
	PlayerA _player;
	
	std::string _fileFmt;
	std::string _fileVerStr;
	UINT16 _fileVerNum;
	bool _looping;
	bool _isRawLog;
	UINT32 _fileStartPos;
	UINT32 _fileEndPos;
	double _volGain;
	
	std::vector<DeviceItem> _chipList;
	std::map<std::string, std::string> _songTags;
	
	size_t _pbSongID;
	size_t _pbSongCnt;
	
	std::string _songPath;
	std::string _playlistPath;
	size_t _playlistTrkID;	// (size_t)-1 when not in a playlist
	size_t _playlistTrkCnt;
	std::string _albumImgPath;
	
	std::vector<SignalHandler> _sigCb;
	std::queue<EventData> _evtQueue;
	bool _enableAlbumImage;
};

#endif	// __MEDIAINFO_HPP__
