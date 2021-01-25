#ifndef __MMKEYS_H__
#define __MMKEYS_H__

#ifdef __cplusplus
extern "C" {
#endif

#include <stdtype.h>

typedef void (*mmkey_cbfunc)(UINT8 event);

#define MMKEY_PLAY	0x01
#define MMKEY_PREV	0x10
#define MMKEY_NEXT	0x11

UINT8 MultimediaKeyHook_Init(void);
void MultimediaKeyHook_Deinit(void);
void MultimediaKeyHook_SetCallback(mmkey_cbfunc callbackFunc);

#ifdef __cplusplus
}
#endif

#endif	// __MMKEYS_H__
