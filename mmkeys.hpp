#ifndef __MMKEYS_HPP__
#define __MMKEYS_HPP__

#include <stdtype.h>
class MediaInfo;

UINT8 MultimediaKeyHook_Init(MediaInfo& mediaInfo);
void MultimediaKeyHook_Deinit(void);

#endif	// __MMKEYS_HPP__
