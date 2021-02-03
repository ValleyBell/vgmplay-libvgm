#include <string>
#include <vector>
#include <map>

#include <stdtype.h>
#include <player/playerbase.hpp>
#include <player/s98player.hpp>
#include <player/droplayer.hpp>
#include <player/vgmplayer.hpp>
#include <player/playera.hpp>
#include <emu/SoundDevs.h>
#include <emu/SoundEmu.h>	// for SndEmu_GetDevName()

#include "mediainfo.hpp"

static void Tags_LangFilter(std::map<std::string, std::string>& tags, const std::string& tagName,
	const std::vector<std::string>& langPostfixes, int defaultLang);
static inline std::string FCC2Str(UINT32 fcc);

void MediaInfo::PreparePlayback(void)
{
	PlayerBase* player = _player.GetPlayer();
	const GeneralOptions& genOpts = _genOpts;
	char verStr[0x20];
	
	EnumerateTags();	// must be done before PreparePlayback(), as it may parse some of the tags
	
	_volGain = 1.0;
	_looping = (player->GetLoopTicks() != (UINT32)-1);
	_fileFmt = player->GetPlayerName();
	_isRawLog = false;
	if (player->GetPlayerType() == FCC_VGM)
	{
		VGMPlayer* vgmplay = dynamic_cast<VGMPlayer*>(player);
		const VGM_HEADER* vgmhdr = vgmplay->GetFileHeader();
		
		_fileVerNum = vgmhdr->fileVer & 0xFFFF;
		sprintf(verStr, "VGM %X.%02X", (vgmhdr->fileVer >> 8) & 0xFF, (vgmhdr->fileVer >> 0) & 0xFF);
		
		_fileEndPos = vgmhdr->dataEnd;
		_fileStartPos = vgmhdr->dataOfs;
		_volGain = pow(2.0, vgmhdr->volumeGain / (double)0x100);
		
		// RAW Log: no loop, no/empty Creator tag, no Title tag
		if (! vgmhdr->loopOfs && _songTags.find("ENCODED_BY") == _songTags.end() &&
			_songTags.find("TITLE") == _songTags.end())
			_isRawLog = true;
		if (vgmhdr->numTicks == 0)
			_isRawLog = false;
	}
	else if (player->GetPlayerType() == FCC_S98)
	{
		S98Player* s98play = dynamic_cast<S98Player*>(player);
		const S98_HEADER* s98hdr = s98play->GetFileHeader();
		
		_fileVerNum = s98hdr->fileVer << 8;
		sprintf(verStr, "S98 v%u", s98hdr->fileVer);
		
		_fileStartPos = s98hdr->dataOfs;
		if (s98hdr->tagOfs > s98hdr->dataOfs)
			_fileEndPos = s98hdr->tagOfs;
		
		if (! s98hdr->loopOfs && _songTags.find("TITLE") == _songTags.end())
			_isRawLog = true;
	}
	else if (player->GetPlayerType() == FCC_DRO)
	{
		DROPlayer* droplay = dynamic_cast<DROPlayer*>(player);
		const DRO_HEADER* drohdr = droplay->GetFileHeader();
		
		_fileVerNum = (drohdr->verMajor << 8) | (drohdr->verMinor << 0);
		sprintf(verStr, "DRO v%u", drohdr->verMajor);	// DRO has a "verMinor" field, but it's always 0
		
		_fileStartPos = drohdr->dataOfs;
		
		_isRawLog = true;
	}
	_fileVerStr = verStr;
	
	return;
}

static void Tags_LangFilter(std::map<std::string, std::string>& tags, const std::string& tagName,
	const std::vector<std::string>& langPostfixes, int defaultLang)
{
	// 1. search for matching lang-tag
	// 2. save that
	// 3. remove all others
	std::vector<std::string> langTags;
	std::vector<std::string>::const_iterator lpfIt;
	std::vector<std::string>::const_iterator ltIt;
	std::map<std::string, std::string>::iterator chosenTagIt;
	
	for (lpfIt = langPostfixes.begin(); lpfIt != langPostfixes.end(); ++lpfIt)
		langTags.push_back(tagName + *lpfIt);
	
	chosenTagIt = tags.end();
	if (defaultLang >= 0 && (size_t)defaultLang < langTags.size())
		chosenTagIt = tags.find(langTags[defaultLang]);
	if (chosenTagIt == tags.end())
	{
		for (ltIt = langTags.begin(); ltIt != langTags.end(); ++ltIt)
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
	
	for (ltIt = langTags.begin(); ltIt != langTags.end(); ++ltIt)
	{
		if (*ltIt == tagName)
			continue;
		tags.erase(*ltIt);	// TODO: iterator handling
	}
	
	return;
}

void MediaInfo::EnumerateTags(void)
{
	PlayerBase* player = _player.GetPlayer();
	std::vector<std::string> langPostfixes;
	int defaultLang = _genOpts.preferJapTag ? 1 : 0;
	
	const char* const* tagList = player->GetTags();
	_songTags.clear();
	if (tagList == NULL)
		return;
	
	for (const char* const* t = tagList; *t != NULL; t += 2)
	{
		if (t[1][0] == '\0')
			continue;	// skip empty VGM tags, else the LangFilter may choose them
		_songTags[t[0]] = t[1];
	}
	
	langPostfixes.push_back("");
	langPostfixes.push_back("-JPN");
	
	Tags_LangFilter(_songTags, "TITLE", langPostfixes, defaultLang);
	Tags_LangFilter(_songTags, "GAME", langPostfixes, defaultLang);
	Tags_LangFilter(_songTags, "SYSTEM", langPostfixes, defaultLang);
	Tags_LangFilter(_songTags, "ARTIST", langPostfixes, defaultLang);
	
	return;
}

const char* MediaInfo::GetSongTagForDisp(const std::string& tagName)
{
	std::map<std::string, std::string>::const_iterator tagIt = _songTags.find(tagName);
	return (tagIt == _songTags.end()) ? "" : tagIt->second.c_str();
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

void MediaInfo::EnumerateChips(void)
{
	PlayerBase* player = _player.GetPlayer();
	std::vector<PLR_DEV_INFO> diList;
	
	player->GetSongDeviceInfo(diList);
	_chipList.clear();
	for (size_t curDev = 0; curDev < diList.size(); curDev ++)
	{
		const PLR_DEV_INFO& pdi = diList[curDev];
		const char* chipName = SndEmu_GetDevName(pdi.type, 0x01, pdi.devCfg);
		if (pdi.type == DEVID_SN76496)
		{
			if (pdi.devCfg->flags & 0x01)
				curDev ++;	// the T6W28 consists of two "half" chips in VGMs
		}
		
		DeviceItem dItm;
		dItm.name = chipName;
		dItm.core = FCC2Str(pdi.core);
		_chipList.push_back(dItm);
	}
	
	return;
}
