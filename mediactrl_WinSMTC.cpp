// huge thanks to the Firefox developers for figuring out how all of this stuff works
#include <windows.h>
#include <stdio.h>

//#define WINDOWS_FOUNDATION_UNIVERSALAPICONTRACT_VERSION	0x10000
#pragma comment(lib, "runtimeobject.lib")
#include <Windows.Media.h>
#include <winsdkver.h>
#include <wrl.h>

#include <stdtype.h>
#include <utils/StrUtils.h>
#include "mediainfo.hpp"
#include "mediactrl.hpp"

namespace WinMedia = ABI::Windows::Media;
namespace WinFoundation = ABI::Windows::Foundation;
namespace WinStrgStrm = ABI::Windows::Storage::Streams;

typedef WinMedia::ISystemMediaTransportControls ISMTC;
typedef WinMedia::SystemMediaTransportControlsProperty SMTCProperty;
typedef WinMedia::ISystemMediaTransportControlsDisplayUpdater ISMTCDisplayUpdater;

typedef WinFoundation::ITypedEventHandler<WinMedia::SystemMediaTransportControls*, WinMedia::SystemMediaTransportControlsButtonPressedEventArgs*> \
	SMTC_ButtonPressEvt_Callback;

#ifndef RuntimeClass_WinMedia_SMTC
#define RuntimeClass_WinMedia_SMTC	L"Windows.Media.SystemMediaTransportControls"
#endif
#ifndef RuntimeClass_WinStrgStrm_RandAccStreamRef
#define RuntimeClass_WinStrgStrm_RandAccStreamRef	L"Windows.Storage.Streams.RandomAccessStreamReference"
#endif

#ifndef ISystemMediaTransportControlsInterop
EXTERN_C const IID IID_ISystemMediaTransportControlsInterop;
MIDL_INTERFACE("ddb0472d-c911-4a1f-86d9-dc3d71a95f5a")
ISystemMediaTransportControlsInterop : public IInspectable
{
public:
	virtual HRESULT STDMETHODCALLTYPE GetForWindow(
		/* [in] */ __RPC__in HWND appWindow,
		/* [in] */ __RPC__in REFIID riid,
		/* [iid_is][retval][out] */ __RPC__deref_out_opt void** mediaTransportControl
	) = 0;
};
#endif	/* __ISystemMediaTransportControlsInterop_INTERFACE_DEFINED__ */


static CPCONV* cpcU8_Wide;
static HWND mWindow;
static MediaInfo* mInf;
static Microsoft::WRL::ComPtr<ISMTC> mSMTCtrl;
static Microsoft::WRL::ComPtr<ISMTCDisplayUpdater> mDispUpd;
static Microsoft::WRL::ComPtr<WinStrgStrm::IRandomAccessStreamReference> mImgStrmRef;
static EventRegistrationToken mBtnPressEvt;
static std::string mLastThumbPath;

static UINT8 HandleMediaKeyPress(WinMedia::SystemMediaTransportControlsButton keycode)
{
	switch(keycode)
	{
	case WinMedia::SystemMediaTransportControlsButton_Play:
		mInf->Event(MI_EVT_PAUSE, MIE_PS_TOGGLE);
		return 0;
	case WinMedia::SystemMediaTransportControlsButton_Pause:
		mInf->Event(MI_EVT_PAUSE, MIE_PS_TOGGLE);
		return 0;
	case WinMedia::SystemMediaTransportControlsButton_Next:
		mInf->Event(MI_EVT_PLIST, MIE_PL_NEXT);
		return 0;
	case WinMedia::SystemMediaTransportControlsButton_Previous:
		mInf->Event(MI_EVT_PLIST, MIE_PL_PREV);
		return 0;
	case WinMedia::SystemMediaTransportControlsButton_Stop:
		mInf->Event(MI_EVT_CONTROL, MIE_CTRL_STOP);
		return 0;
	//case WinMedia::SystemMediaTransportControlsButton_FastForward:
	//	return 0;
	//case WinMedia::SystemMediaTransportControlsButton_Rewind:
	//	return 0;
	}
	
	return 1;
}

static void RefreshPlaybackState(void)
{
	WinMedia::MediaPlaybackStatus pbStat;
	
	if (! (mInf->_playState & PLAYSTATE_PLAY))
		pbStat = WinMedia::MediaPlaybackStatus_Stopped;
	else if (mInf->_playState & PLAYSTATE_PAUSE)
		pbStat = WinMedia::MediaPlaybackStatus_Paused;
	else
		pbStat = WinMedia::MediaPlaybackStatus_Playing;
	HRESULT hRes = mSMTCtrl->put_PlaybackStatus(pbStat);
	if (! SUCCEEDED(hRes))
		printf("Error setting SMTC playback state!");
	
	return;
}

static bool InitDisplayAndControls()
{
	HRESULT hRes;
	Microsoft::WRL::ComPtr<ISystemMediaTransportControlsInterop> interop;
	
	hRes = WinFoundation::GetActivationFactory(
		Microsoft::WRL::Wrappers::HStringReference(RuntimeClass_WinMedia_SMTC).Get(),
		interop.GetAddressOf());
	if (FAILED(hRes))
	{
		printf("SMTC: Unable to create SMTCInterop object!\n");
		return false;
	}
	if (! interop)
		return false;
	
	mWindow = GetConsoleWindow();
	hRes = interop->GetForWindow(mWindow, IID_PPV_ARGS(mSMTCtrl.GetAddressOf()));
	if (FAILED(hRes))
	{
		printf("SMTC: interop->GetForWindow failed!\n");
		return false;
	}
	if (! mSMTCtrl)
		return false;
	
	hRes = mSMTCtrl->get_DisplayUpdater(mDispUpd.GetAddressOf());
	if (FAILED(hRes))
	{
		printf("SMTC: GetDisplayUpdater failed!\n");
		return false;
	}
	if (! mDispUpd)
		return false;
	
	return true;
}

static HRESULT ButtonCallback(WinMedia::ISystemMediaTransportControls*, WinMedia::ISystemMediaTransportControlsButtonPressedEventArgs* pArgs)
{
	if (pArgs == NULL)
		return S_FALSE;
	WinMedia::SystemMediaTransportControlsButton btn;
	
	HRESULT hRes = pArgs->get_Button(&btn);
	if (FAILED(hRes))
	{
		printf("SMTC ButtonPressedEvent: Unable to get Button!");
		return S_FALSE;
	}
	
	HandleMediaKeyPress(btn);
	return S_OK;
}

static bool RegisterEvents(void)
{
	if (! mSMTCtrl)
		return true;
	
	Microsoft::WRL::ComPtr<SMTC_ButtonPressEvt_Callback> callbackbtnPressed =
		Microsoft::WRL::Callback<SMTC_ButtonPressEvt_Callback>(ButtonCallback);
	HRESULT hRes = mSMTCtrl->add_ButtonPressed(callbackbtnPressed.Get(), &mBtnPressEvt);
	if (FAILED(hRes))
	{
		printf("SMTC: Unable to install callback!");
		return false;
	}
	
	return true;
}

static void UnregisterEvents(void)
{
	if (! mSMTCtrl)
		return;
	if (mBtnPressEvt.value != 0)
		mSMTCtrl->remove_ButtonPressed(mBtnPressEvt);
	return;
}

static wchar_t* StrUTF8toUTF16(const char* strUTF8)
{
	if (strUTF8 == NULL)
		return NULL;
	
	size_t tempLen = 0;
	wchar_t* result = NULL;
	UINT8 retVal = CPConv_StrConvert(cpcU8_Wide, &tempLen, reinterpret_cast<char**>(&result), 0, strUTF8);
	if (retVal & 0x80)
		tempLen = 0;
	if (retVal && tempLen == 0)
	{
		free(result);
		return NULL;
	}
	return result;
}

static void SetMetadata(void)
{
	HRESULT hRes;
	Microsoft::WRL::ComPtr<WinMedia::IMusicDisplayProperties> musicProps;
	
	mDispUpd->put_Type(WinMedia::MediaPlaybackType::MediaPlaybackType_Music);
	hRes = mDispUpd->get_MusicProperties(musicProps.GetAddressOf());
	if (FAILED(hRes))
	{
		printf("SMTC: Failed to get music properties!\n");
		return;
	}
	
	wchar_t* artistWStr = StrUTF8toUTF16(mInf->GetSongTagForDisp("AUTHOR"));
	wchar_t* titleWStr = StrUTF8toUTF16(mInf->GetSongTagForDisp("TITLE"));
	wchar_t* albumWStr = StrUTF8toUTF16(mInf->GetSongTagForDisp("GAME"));
	
	hRes = musicProps->put_Artist(Microsoft::WRL::Wrappers::HStringReference(artistWStr).Get());
	if (FAILED(hRes))
		printf("SMTC: Failed to set artist\n");
	hRes = musicProps->put_Title(Microsoft::WRL::Wrappers::HStringReference(titleWStr).Get());
	if (FAILED(hRes))
		printf("SMTC: Failed to set title\n");
	hRes = musicProps->put_AlbumArtist(Microsoft::WRL::Wrappers::HStringReference(albumWStr).Get());
	if (FAILED(hRes))
		printf("SMTC: Failed to set album\n");
	
	free(artistWStr);
	free(titleWStr);
	free(albumWStr);
	
	hRes = mDispUpd->Update();
	if (FAILED(hRes))
	{
		printf("SMTC: Failed to update metadata!");
		return;
	}
	
	return;
}

static void ClearThumbnail()
{
	if (mDispUpd == NULL)
		return;
	
	mDispUpd->put_Thumbnail(nullptr);
	mLastThumbPath = std::string();
	mDispUpd->Update();
	
	return;
}

static void SetThumbnail(const std::string& filePath)
{
	if (mDispUpd == NULL)
		return;
	if (mLastThumbPath == filePath)
		return;
	
	if (filePath.empty() || true)
	{
		ClearThumbnail();
		return;
	}
	
	HRESULT hRes;
	Microsoft::WRL::ComPtr<WinStrgStrm::IRandomAccessStreamReferenceStatics> streamRefFactory;
	
	hRes = WinFoundation::GetActivationFactory(
		Microsoft::WRL::Wrappers::HStringReference(RuntimeClass_WinStrgStrm_RandAccStreamRef).Get(),
		streamRefFactory.GetAddressOf());
	if (FAILED(hRes))
	{
		printf("SMTC: Unable to create RandomAccessStreamReferenceStatics object!\n");
		return;
	}
	
	Microsoft::WRL::ComPtr<ABI::Windows::Storage::IStorageFile> file;
	// TODO: use StorageFile.GetFileFromPathAsync(filePath) here to get "file"
	
	hRes = streamRefFactory->CreateFromFile(file.Get(), mImgStrmRef.GetAddressOf());
	if (FAILED(hRes))
	{
		printf("SMTC: Failed to create ImageStreamReference!\n");
		return;
	}
	
	hRes = mDispUpd->put_Thumbnail(mImgStrmRef.Get());
	if (FAILED(hRes))
	{
		printf("SMTC: Failed to set thumbnail\n");
		return;
	}
	mLastThumbPath = filePath;
	
	hRes = mDispUpd->Update();
	if (FAILED(hRes))
	{
		printf("SMTC: Failed to update thumbnail\n");
		return;
	}
	
	//mThumbnailUrl = aUrl;
	
	return;
}


UINT8 MediaControl::Init(MediaInfo& mediaInfo)
{
	if (mSMTCtrl)
		return 0x01;
	
	bool retB;
	UINT8 retVal;
	
	mInf = &mediaInfo;
	mInf->_enableAlbumImage = true;
	
	retB = InitDisplayAndControls();
	if (! retB)
		return 0xFF;
	retB = RegisterEvents();
	if (! retB)
	{
		Deinit();
		return 0xFE;
	}
	
	retVal = CPConv_Init(&cpcU8_Wide, "UTF-8", "UTF-16LE");
	if (retVal & 0x80)
		cpcU8_Wide = NULL;
	
	mSMTCtrl->put_IsPlayEnabled(true);
	mSMTCtrl->put_IsPauseEnabled(true);
	mSMTCtrl->put_IsPreviousEnabled(false);
	mSMTCtrl->put_IsNextEnabled(false);
	HRESULT hRes = mSMTCtrl->put_IsEnabled(true);
	if (FAILED(hRes))
		return 0x01;
	
	mInf->AddSignalCallback(&MediaControl::SignalCB, this);
	mSMTCtrl->put_PlaybackStatus(WinMedia::MediaPlaybackStatus_Closed);
	ClearThumbnail();
	
	return 0x00;
}

void MediaControl::Deinit(void)
{
	if (! mSMTCtrl)
		return;
	
	mSMTCtrl->put_PlaybackStatus(WinMedia::MediaPlaybackStatus_Closed);
	UnregisterEvents();
	ClearThumbnail();
	mSMTCtrl->put_IsEnabled(false);
	
	if (cpcU8_Wide != NULL)
	{
		CPConv_Deinit(cpcU8_Wide);
		cpcU8_Wide = NULL;
	}
	
	return;
}

void MediaControl::ReadWriteDispatch(void)
{
	return;
}

/*static*/ void MediaControl::SignalCB(MediaInfo* mInfo, void* userParam, UINT8 signalMask)
{
	MediaControl* obj = static_cast<MediaControl*>(userParam);
	obj->SignalHandler(signalMask);
	return;
}

void MediaControl::SignalHandler(UINT8 signalMask)
{
	if (signalMask & MI_SIG_NEW_SONG)
	{
		mSMTCtrl->put_IsPreviousEnabled(mInf->_pbSongID > 0);
		mSMTCtrl->put_IsNextEnabled(mInf->_pbSongID + 1 < mInf->_pbSongCnt);
		SetMetadata();
		SetThumbnail(mInf->_albumImgPath);
	}
	if (signalMask & (MI_SIG_NEW_SONG | MI_SIG_PLAY_STATE))
		RefreshPlaybackState();
	//if (signalMask & MI_SIG_POSITION)
	//if (signalMask & MI_SIG_VOLUME)
	return;
}
