#include <windows.h>
#include <stdio.h>

#include <stdtype.h>
#include "mediainfo.hpp"
#include "mediactrl.hpp"
#include "mediactrl_WinKeys.hpp"

#ifndef VK_MEDIA_NEXT_TRACK
// from WinUser.h
#define VK_MEDIA_NEXT_TRACK    0xB0
#define VK_MEDIA_PREV_TRACK    0xB1
#define VK_MEDIA_STOP          0xB2
#define VK_MEDIA_PLAY_PAUSE    0xB3
#endif

static DWORD idThread = 0;
static HANDLE hThread = NULL;
static HANDLE hEvent = NULL;
static MediaInfo* mInf;

static UINT8 HandleMediaKeyPress(int evtCode)
{
	switch(evtCode)
	{
	case VK_MEDIA_PLAY_PAUSE:
		mInf->Event(MI_EVT_PAUSE, MIE_PS_TOGGLE);
		break;
	case VK_MEDIA_STOP:
		mInf->Event(MI_EVT_CONTROL, MIE_CTRL_STOP);
		break;
	case VK_MEDIA_PREV_TRACK:
		mInf->Event(MI_EVT_PLIST, MIE_PL_PREV);
		break;
	case VK_MEDIA_NEXT_TRACK:
		mInf->Event(MI_EVT_PLIST, MIE_PL_NEXT);
		break;
	}
	
	return 1;
}

static DWORD WINAPI KeyMessageThread(void* args)
{
	MSG msg;
	BOOL retValB;
	
	// enforce creation of message queue
	PeekMessage(&msg, NULL, WM_USER, WM_USER, PM_NOREMOVE);
	
	retValB = RegisterHotKey(NULL, VK_MEDIA_PLAY_PAUSE, 0, VK_MEDIA_PLAY_PAUSE);
	retValB = RegisterHotKey(NULL, VK_MEDIA_STOP, 0, VK_MEDIA_STOP);
	retValB = RegisterHotKey(NULL, VK_MEDIA_PREV_TRACK, 0, VK_MEDIA_PREV_TRACK);
	retValB = RegisterHotKey(NULL, VK_MEDIA_NEXT_TRACK, 0, VK_MEDIA_NEXT_TRACK);
	
	SetEvent(hEvent);
	
	while(retValB = GetMessage(&msg, NULL, 0, 0))
	{
		if (msg.message == WM_HOTKEY)
			HandleMediaKeyPress((int)msg.wParam);
	}
	
	UnregisterHotKey(NULL, VK_MEDIA_PLAY_PAUSE);
	UnregisterHotKey(NULL, VK_MEDIA_STOP);
	UnregisterHotKey(NULL, VK_MEDIA_PREV_TRACK);
	UnregisterHotKey(NULL, VK_MEDIA_NEXT_TRACK);
	
	return 0;
}

MediaCtrlMKeys::MediaCtrlMKeys()
{
}

MediaCtrlMKeys::~MediaCtrlMKeys()
{
	if (hThread != NULL)
		Deinit();
}

UINT8 MediaCtrlMKeys::Init(MediaInfo& mediaInfo)
{
	if (hThread != NULL)
		return 0x01;
	
	mInf = &mediaInfo;
	mInf->_enableAlbumImage = false;
	hEvent = CreateEvent(NULL, FALSE, FALSE, NULL);
	hThread = CreateThread(NULL, 0x00, KeyMessageThread, NULL, 0x00, &idThread);
	if (hThread == NULL)
		return 0xFF;		// CreateThread failed
	
	WaitForSingleObject(hEvent, INFINITE);
	
	return 0x00;
}

void MediaCtrlMKeys::Deinit(void)
{
	if (hThread == NULL)
		return;
	
	PostThreadMessage(idThread, WM_QUIT, 0, 0);
	WaitForSingleObject(hThread, INFINITE);
	
	CloseHandle(hThread);
	hThread = NULL;
	
	return;
}

void MediaCtrlMKeys::SignalHandler(UINT8 signalMask)
{
	return;
}
