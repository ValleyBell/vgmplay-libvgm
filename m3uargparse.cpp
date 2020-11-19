#include <string.h>
#include <string>
#include <vector>
#include <fstream>
#include <istream>
#ifdef _WIN32
#include <Windows.h>
#else
#include <iconv.h>
#endif

#include "stdtype.h"
#include "utils.hpp"
#include "m3uargparse.hpp"


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
static std::string WinStr2UTF8(const std::string& str);


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
	const char* strPtr;
	char fileSig[0x03];
	bool isUTF8;
	bool isV2Fmt;
	size_t METASTR_LEN;
	UINT32 lineNo;
	std::string tempStr;
	size_t songID;
	
#if defined(WIN32) && _MSC_VER >= 1400
	std::wstring fileNameW;
	fileNameW.resize(MultiByteToWideChar(CP_UTF8, 0, fileName, -1, NULL, 0) - 1);
	MultiByteToWideChar(CP_UTF8, 0, fileName, -1, &fileNameW[0], fileNameW.size());
	hFile.open(fileNameW);
#else
	hFile.open(fileName);
#endif
	if (! hFile.is_open())
		return false;
	
	strPtr = GetFileTitle(fileName);
	baseDir = std::string(fileName, strPtr - fileName);
	
	memset(fileSig, 0x00, 3);
	hFile.read(fileSig, 3);
	isUTF8 = ! memcmp(fileSig, UTF8_SIG, 3);
	if (! isUTF8)
		hFile.seekg(0, std::ios_base::beg);
	isUTF8 |= isM3Uu8;
	
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
		
		if (! isUTF8)
			tempStr = WinStr2UTF8(tempStr);
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
	
	hFile.close();
	
	return true;
}

static std::string WinStr2UTF8(const std::string& str)
{
#ifdef _WIN32
	std::wstring wtemp;
	std::string out;
	int numChars;
	
	// char -> wchar_t using local ANSI codepage
	numChars = MultiByteToWideChar(CP_ACP, 0, str.c_str(), -1, NULL, 0);
	if (numChars < 0)
		return out;
	wtemp.resize(numChars - 1);
	MultiByteToWideChar(CP_ACP, 0, str.c_str(), -1, &wtemp[0], wtemp.size());
	
	// convert wchar_t to char UTF-8
	numChars = WideCharToMultiByte(CP_UTF8, 0, wtemp.c_str(), -1, NULL, 0, NULL, NULL);
	if (numChars < 0)
		return out;
	out.resize(numChars - 1);
	WideCharToMultiByte(CP_UTF8, 0, wtemp.c_str(), -1, &out[0], out.size(), NULL, NULL);
	
	return out;
#else
	// TODO: handle Windows codepages
	return str;
#endif
}
