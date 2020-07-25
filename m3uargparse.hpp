#ifndef __M3UARGPARSE_HPP__
#define __M3UARGPARSE_HPP__

#include <stddef.h>
#include <string>
#include <vector>
#include "stdtype.h"

struct SongFileList
{
	std::string fileName;
	size_t playlistID;
};

UINT8 ParseSongFiles(const std::vector<char*>& args, std::vector<SongFileList>& songList, std::vector<std::string>& playlistList);
UINT8 ParseSongFiles(const std::vector<const char*>& args, std::vector<SongFileList>& songList, std::vector<std::string>& playlistList);
UINT8 ParseSongFiles(const std::vector<std::string>& args, std::vector<SongFileList>& songList, std::vector<std::string>& playlistList);
UINT8 ParseSongFiles(size_t argc, const char* const* argv, std::vector<SongFileList>& songList, std::vector<std::string>& playlistList);

#endif	// __M3UARGPARSE_HPP__
