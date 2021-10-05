#include <string>
#include <vector>
#include <map>
#include <math.h>
#ifdef _WIN32
#include <windows.h>
#else
#include <glob.h>
#endif

#include <stdtype.h>
#include <player/playerbase.hpp>
#include <player/s98player.hpp>
#include <player/droplayer.hpp>
#include <player/vgmplayer.hpp>
#include <player/playera.hpp>
#include <emu/SoundDevs.h>
#include <emu/SoundEmu.h>	// for SndEmu_GetDevName()
#include <utils/StrUtils.h>

#include "utils.hpp"
#include "mediainfo.hpp"

//#define DEBUG_ART_SEARCH	1

static void Tags_LangFilter(std::map<std::string, std::string>& tags, const std::string& tagName,
	const std::vector<std::string>& langPostfixes, int defaultLang);
static inline std::string FCC2Str(UINT32 fcc);

MediaInfo::MediaInfo()
{
#ifdef _WIN32
	_cpcUTF8toAPI = NULL;
	_cpcAPItoUTF8 = NULL;
	CPConv_Init(&_cpcUTF8toAPI, "UTF-8", "UTF-16LE");
	CPConv_Init(&_cpcAPItoUTF8, "UTF-16LE", "UTF-8");
#endif
}

MediaInfo::~MediaInfo()
{
#ifdef _WIN32
	CPConv_Deinit(_cpcUTF8toAPI);
	CPConv_Deinit(_cpcAPItoUTF8);
#endif
}

void MediaInfo::PreparePlayback(void)
{
	PlayerBase* player = _player.GetPlayer();
	PLR_SONG_INFO sInf;
	char verStr[0x20];
	
	EnumerateTags();	// must be done before PreparePlayback(), as it may parse some of the tags
	
	player->GetSongInfo(sInf);
	_volGain = 1.0;
	// Note: sInf.loopTick is -1 for non-looping songs. player->GetLoopTicks() returns 0 that case.
	_looping = (sInf.loopTick != (UINT32)-1);
	_fileFmt = player->GetPlayerName();
	_fileVerNum = (sInf.fileVerMaj << 8) | (sInf.fileVerMin << 0);
	_isRawLog = false;
	if (player->GetPlayerType() == FCC_VGM)
	{
		VGMPlayer* vgmplay = dynamic_cast<VGMPlayer*>(player);
		const VGM_HEADER* vgmhdr = vgmplay->GetFileHeader();
		
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
		tags.erase(*ltIt);
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

#ifdef _WIN32
std::wstring MediaInfo::CharConv_UTF8toAPI(const std::string& textU8)
{
	size_t textWLen = 0;
	wchar_t* textWStr = NULL;
	UINT8 retVal = CPConv_StrConvert(_cpcUTF8toAPI, &textWLen, reinterpret_cast<char**>(&textWStr),
		textU8.length(), textU8.c_str());
	
	std::wstring result;
	if (retVal < 0x80)
		result.assign(textWStr, textWStr + textWLen / sizeof(wchar_t));
	free(textWStr);
	return result;
}

std::string MediaInfo::CharConv_APItoUTF8(const std::wstring& textW)
{
	size_t textU8Len = 0;
	char* textU8Str = NULL;
	UINT8 retVal = CPConv_StrConvert(_cpcAPItoUTF8, &textU8Len, &textU8Str,
		textW.length() * sizeof(wchar_t), reinterpret_cast<const char*>(textW.c_str()));
	
	std::string result;
	if (retVal < 0x80)
		result.assign(textU8Str, textU8Str + textU8Len);
	free(textU8Str);
	return result;
}
#endif

bool MediaInfo::FileExists(const std::string& fileName)
{
#ifdef _WIN32
	std::wstring fileNameW = CharConv_UTF8toAPI(fileName);
	FILE* fp = _wfopen(fileNameW.c_str(), L"rb");
#else
	FILE* fp = fopen(fileName.c_str(), "rb");
#endif
	if (fp == NULL)
		return false;
	fclose(fp);
	return true;
}

void MediaInfo::SearchAlbumImage(void)
{
	if (! _enableAlbumImage)
		return;
	
	// Thanks to Tasos Sahanidis for the art search algorithm.
#if DEBUG_ART_SEARCH
	printf("Starting art search.\n");
#endif
	
	_albumImgPath = std::string();
	if (_playlistTrkID != (size_t)-1)
	{
		// for "abc/d/playlist.m3u", try "abc/d/playlist.png"
		const char* fileExt = GetFileExtension(_playlistPath.c_str());
		size_t dotPos = (fileExt != NULL) ? (fileExt - 1 - _playlistPath.c_str()) : _playlistPath.size();
		std::string imgPath = _playlistPath.substr(0, dotPos) + ".png";
#if DEBUG_ART_SEARCH
		printf("Trying %s\n", imgPath.c_str());
#endif
		if (FileExists(imgPath))
		{
			_albumImgPath = imgPath;
			return;
		}
	}
	
	std::string basePath;
	{
		const char* filePath = _songPath.c_str();
		const char* fileTitle = GetFileTitle(filePath);
		basePath = _songPath.substr(0, fileTitle - filePath);
	}
	
	// If we get here, we're probably in single track mode, or the playlist is named differently.
	// So we check [base path] + [album] + ".png"
	const char* albumName = GetSongTagForDisp("GAME");
	if (albumName[0] != '\0')
	{
		std::string imgPath = basePath + albumName + ".png";
#if DEBUG_ART_SEARCH
		printf("Trying %s\n", imgPath.c_str());
#endif
		if (FileExists(imgPath))
		{
			_albumImgPath = imgPath;
			return;
		}
	}
	
	// As a last resort, pick the first image glob can find in the base path.
	// Append the case insensitive extension to the base path.
#ifdef _WIN32
	{
		std::string pathPattern = basePath + "*.png";
#if DEBUG_ART_SEARCH
		printf("Using file search %s\n", pathPattern.c_str());
#endif
		
		std::wstring pathPatW = CharConv_UTF8toAPI(pathPattern);
		WIN32_FIND_DATAW ffd;
		HANDLE hFind = FindFirstFileW(pathPatW.c_str(), &ffd);
		if (hFind != INVALID_HANDLE_VALUE)
		{
			std::string fileNameU8 = CharConv_APItoUTF8(ffd.cFileName);
			_albumImgPath = CombinePaths(basePath, fileNameU8);
			FindClose(hFind);
			return;
		}
	}
#else
	{
		std::string pathPattern = basePath + "*.[pP][nN][gG]";
#if DEBUG_ART_SEARCH
		printf("Using glob %s\n", pathPattern.c_str());
#endif
		glob_t result;
		int ret = glob(pathPattern.c_str(), GLOB_NOSORT, NULL, &result);
		if (ret == 0)
		{
			if (result.gl_pathc > 0)
			{
				_albumImgPath = result.gl_pathv[0];
				globfree(&result);
				return;
			}
		}
		globfree(&result);
	}
#endif
	
#if DEBUG_ART_SEARCH
	printf("Art search failed.\n");
#endif
	return;
}

void MediaInfo::AddSignalCallback(MI_SIGNAL_CB func, void* param)
{
	SignalHandler scb = {func, param};
	_sigCb.push_back(scb);
	return;
}

void MediaInfo::RemoveSignalCallback(MI_SIGNAL_CB func, void* param)
{
	std::vector<SignalHandler>::iterator scbIt;
	for (scbIt = _sigCb.begin(); scbIt != _sigCb.end(); ++scbIt)
	{
		if (scbIt->func == func && scbIt->param == param)
		{
			_sigCb.erase(scbIt);
			break;
		}
	}
	return;
}

void MediaInfo::Event(UINT8 evtType, INT32 evtParam)
{
	EventData ed = {evtType, evtParam};
	_evtQueue.push(ed);
	return;
}

void MediaInfo::Signal(UINT8 signalMask)
{
	std::vector<SignalHandler>::iterator scbIt;
	for (scbIt = _sigCb.begin(); scbIt != _sigCb.end(); ++scbIt)
		scbIt->func(this, scbIt->param, signalMask);
	return;
}
