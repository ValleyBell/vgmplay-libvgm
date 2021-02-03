#ifndef __MEDIAINFO_HPP__
#define __MEDIAINFO_HPP__

#include <string>
#include <vector>
#include <map>

#include <stdtype.h>
#include <player/playera.hpp>
#include "playcfg.hpp"

class MediaInfo
{
public:
	void PreparePlayback(void);
	const char* GetSongTagForDisp(const std::string& tagName);
	void EnumerateTags(void);	// implicitly called by PreparePlayback(), as that one may parse some of the tags
	void EnumerateChips(void);	// must be called after starting playback in order to retrieve used sound core IDs
	
	struct DeviceItem
	{
		std::string name;
		std::string core;
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
	
	// TODO: event stuff
};

#endif	// __MEDIAINFO_HPP__
