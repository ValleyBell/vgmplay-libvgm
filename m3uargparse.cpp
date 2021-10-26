#include <string.h>
#include <string>
#include <vector>
#include <fstream>
#include <istream>
#ifdef _WIN32
#include <Windows.h>	// for file name charset conversion
#include <wchar.h>	// for UTF-16 file name functions
#endif
#include <utils/StrUtils.h>

#include "stdtype.h"
#include "utils.hpp"
#include "m3uargparse.hpp"

#ifdef _WIN32
#if _MSC_VER >= 1400 || __GNUC__ >= 8
#define USE_OPEN_W
#endif
#endif


#ifdef _MSC_VER
#define	stricmp	_stricmp
#else
#define	stricmp	strcasecmp
#endif

static const char* M3UV2_HEAD = "#EXTM3U";
static const char* M3UV2_META = "#EXTINF:";
static const UINT8 UTF8_SIG[] = {0xEF, 0xBB, 0xBF};


//UINT8 ParseSongFiles(const std::vector<char*>& args, std::vector<SongFileList>& songList, std::vector<PlaylistFileList>& playlistList);
//UINT8 ParseSongFiles(const std::vector<const char*>& args, std::vector<SongFileList>& songList, std::vector<PlaylistFileList>& playlistList);
//UINT8 ParseSongFiles(const std::vector<std::string>& args, std::vector<SongFileList>& songList, std::vector<PlaylistFileList>& playlistList);
//UINT8 ParseSongFiles(size_t argc, const char* const* argv, std::vector<SongFileList>& songList, std::vector<PlaylistFileList>& playlistList);
static bool ReadM3UPlaylist(const char* fileName, std::vector<SongFileList>& songList, bool isM3Uu8);


UINT8 ParseSongFiles(const std::vector<char*>& args, std::vector<SongFileList>& songList, std::vector<PlaylistFileList>& playlistList)
{
	const char* const* argv = args.empty() ? NULL : &args[0];
	return ParseSongFiles(args.size(), argv, songList, playlistList);
}

UINT8 ParseSongFiles(const std::vector<const char*>& args, std::vector<SongFileList>& songList, std::vector<PlaylistFileList>& playlistList)
{
	const char* const* argv = args.empty() ? NULL : &args[0];
	return ParseSongFiles(args.size(), argv, songList, playlistList);
}

UINT8 ParseSongFiles(const std::vector<std::string>& args, std::vector<SongFileList>& songList, std::vector<PlaylistFileList>& playlistList)
{
	std::vector<const char*> argVec(args.size());
	size_t curArg;
	
	for (curArg = 0; curArg < args.size(); curArg ++)
		argVec[curArg] = args[curArg].c_str();
	
	return ParseSongFiles(argVec, songList, playlistList);
}

UINT8 ParseSongFiles(size_t argc, const char* const* argv, std::vector<SongFileList>& songList, std::vector<PlaylistFileList>& playlistList)
{
	size_t curArg;
	const char* fileName;
	const char* fileExt;
	UINT8 resVal;
	bool retValB;
	
	songList.clear();
	playlistList.clear();
	resVal = 0x00;
	for (curArg = 0; curArg < argc; curArg ++)
	{
		fileName = argv[curArg];
		fileExt = GetFileExtension(fileName);
		if (fileExt == NULL)
			fileExt = "";
		if (! stricmp(fileExt, "m3u") || ! stricmp(fileExt, "m3u8"))
		{
			size_t plSong = songList.size();
			
			retValB = ReadM3UPlaylist(fileName, songList, ! stricmp(fileExt, "m3u8"));
			if (! retValB)
			{
				resVal |= 0x01;
				continue;
			}
			
			PlaylistFileList pfl;
			pfl.fileName = fileName;
			pfl.songCount = songList.size() - plSong;
			for (; plSong < songList.size(); plSong ++)
				songList[plSong].playlistID = playlistList.size();
			playlistList.push_back(pfl);
		}
		else
		{
			SongFileList sfl;
			sfl.fileName = fileName;
			sfl.playlistID = (size_t)-1;
			sfl.playlistSongID = (size_t)-1;
			songList.push_back(sfl);
		}
	}
	
	return 0x00;
}

static bool ReadM3UPlaylist(const char* fileName, std::vector<SongFileList>& songList, bool isM3Uu8)
{
	std::ifstream hFile;
	std::string baseDir;
	char fileSig[0x03];
	bool isUTF8;
	bool isV2Fmt;
	size_t METASTR_LEN;
	UINT32 lineNo;
	std::string tempStr;
	size_t songID;
	CPCONV* cpcU8;
	
#if defined(_WIN32) && defined(USE_OPEN_W)
	// not using libvgm CPConv here, because using WinAPIs doesn't need separate initialization
	std::wstring fileNameW;
	fileNameW.resize(MultiByteToWideChar(CP_UTF8, 0, fileName, -1, NULL, 0) - 1);
	MultiByteToWideChar(CP_UTF8, 0, fileName, -1, &fileNameW[0], fileNameW.size());
	hFile.open(fileNameW.c_str());	// use "const wchar_t*" for MinGW implementations without wstring overload
#else
	hFile.open(fileName);
#endif
	if (! hFile.is_open())
		return false;
	
	baseDir = std::string(fileName, GetFileTitle(fileName) - fileName);
	
	memset(fileSig, 0x00, 3);
	hFile.read(fileSig, 3);
	isUTF8 = ! memcmp(fileSig, UTF8_SIG, 3);	// check for UTF-8 BOM
	if (! isUTF8)
		hFile.seekg(0, std::ios_base::beg);
	isUTF8 |= isM3Uu8;
	
	cpcU8 = NULL;
	if (! isUTF8)
		CPConv_Init(&cpcU8, "CP1252", "UTF-8");
	
	isV2Fmt = false;
	METASTR_LEN = strlen(M3UV2_META);
	lineNo = 0;
	songID = 0;
	while(hFile.good() && ! hFile.eof())
	{
		std::getline(hFile, tempStr);
		lineNo ++;
		
		while(! tempStr.empty() && iscntrl((unsigned char)tempStr[tempStr.size() - 1]))
			tempStr.resize(tempStr.size() - 1);	// remove NewLine-Characters
		if (tempStr.empty())
			continue;
		
		if (lineNo == 1 && tempStr == M3UV2_HEAD)
		{
			isV2Fmt = true;
			continue;
		}
		if (isV2Fmt && ! tempStr.compare(0, METASTR_LEN, M3UV2_META))
		{
			// Ignore metadata of m3u v2
			lineNo ++;
			continue;
		}
		
		if (cpcU8 != NULL)
		{
			size_t tempU8Len = 0;
			char* tempU8Str = NULL;
			UINT8 retVal = CPConv_StrConvert(cpcU8, &tempU8Len, &tempU8Str, tempStr.length(), tempStr.c_str());
			if (retVal < 0x80)
				tempStr.assign(tempU8Str, tempU8Str + tempU8Len);
			free(tempU8Str);
		}
		// at this point, we should have UTF-8 file names
		
		SongFileList sfl;
		sfl.fileName = CombinePaths(baseDir, tempStr);
		sfl.playlistID = (size_t)-1;
		sfl.playlistSongID = songID;
#ifndef _WIN32	// on Unix systems, make sure to turn '\' into '/'
		StandardizeDirSeparators(sfl.fileName);
#endif
		songList.push_back(sfl);
		songID ++;
	}
	
	if (cpcU8 != NULL)
		CPConv_Deinit(cpcU8);
	
	hFile.close();
	
	return true;
}
