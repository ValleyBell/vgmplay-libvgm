// huge thanks to the Firefox developers for figuring out how all of this stuff works
#include <windows.h>
#include <stdio.h>

//#define WINDOWS_FOUNDATION_UNIVERSALAPICONTRACT_VERSION	0x10000
#include <Windows.Media.h>
#include <winsdkver.h>
#include <wrl.h>

#include <stdtype.h>
#include <utils/StrUtils.h>
#include "mediainfo.hpp"
#include "mediactrl.hpp"


namespace MsWRL = Microsoft::WRL;
namespace WinMedia = ABI::Windows::Media;
namespace WinFoundation = ABI::Windows::Foundation;
namespace WinStrg = ABI::Windows::Storage;
namespace WinStrgStrm = ABI::Windows::Storage::Streams;

typedef WinMedia::ISystemMediaTransportControls ISMTC;
typedef WinMedia::SystemMediaTransportControlsProperty SMTCProperty;
typedef WinMedia::ISystemMediaTransportControlsDisplayUpdater ISMTCDisplayUpdater;

typedef WinFoundation::ITypedEventHandler<WinMedia::SystemMediaTransportControls*, WinMedia::SystemMediaTransportControlsButtonPressedEventArgs*> \
	SMTC_ButtonPressEvt_Callback;

#define RuntimeClass_WinMedia_SMTC	L"Windows.Media.SystemMediaTransportControls"
#define RuntimeClass_WinStrgStrm_RandAccStreamRef	L"Windows.Storage.Streams.RandomAccessStreamReference"
#define RuntimeClass_WinStrg_StorageFile	L"Windows.Storage.StorageFile"

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


static UINT8 HandleMediaKeyPress(WinMedia::SystemMediaTransportControlsButton keycode);
static void RefreshPlaybackState(void);
static bool InitDisplayAndControls(void);
static HRESULT ButtonCallback(WinMedia::ISystemMediaTransportControls*, WinMedia::ISystemMediaTransportControlsButtonPressedEventArgs* pArgs);
static bool RegisterEvents(void);
static void UnregisterEvents(void);

static wchar_t* StrUTF8toUTF16(const char* strUTF8);
static void SetMetadata(void);
static void CancelAsync(void);
static void ClearThumbnail();
static HRESULT StorageFileAsyncCallback(WinFoundation::IAsyncOperation<WinStrg::StorageFile*>* aAsyncOp, AsyncStatus aStatus);
static void SetThumbnail(const std::string& filePath);
static void SetThumbnail_Exec(MsWRL::ComPtr<WinStrg::IStorageFile> thumbISF);


static CPCONV* cpcU8_Wide;
static HWND mWindow;
static MediaInfo* mInf;
static MsWRL::ComPtr<ISMTC> mSMTCtrl;
static MsWRL::ComPtr<ISMTCDisplayUpdater> mDispUpd;
static EventRegistrationToken mBtnPressEvt;

static MsWRL::ComPtr< WinFoundation::IAsyncOperation<WinStrg::StorageFile*> > mSFileAsyncOp;
static std::string mThumbPathWIP;	// thumbnail file path while async operations are running
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
		printf("SMTC: Error setting playback state!");
	
	return;
}

static bool InitDisplayAndControls(void)
{
	HRESULT hRes;
	MsWRL::ComPtr<ISystemMediaTransportControlsInterop> interop;
	
	hRes = WinFoundation::GetActivationFactory(
		MsWRL::Wrappers::HStringReference(RuntimeClass_WinMedia_SMTC).Get(),
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
	
	MsWRL::ComPtr<SMTC_ButtonPressEvt_Callback> callbackbtnPressed =
		MsWRL::Callback<SMTC_ButtonPressEvt_Callback>(ButtonCallback);
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
	MsWRL::ComPtr<WinMedia::IMusicDisplayProperties> musicProps;
	MsWRL::ComPtr<WinMedia::IMusicDisplayProperties2> musicProp2;
	MsWRL::ComPtr<WinMedia::IMusicDisplayProperties3> musicProp3;
	
	mDispUpd->put_Type(WinMedia::MediaPlaybackType::MediaPlaybackType_Music);
	hRes = mDispUpd->get_MusicProperties(musicProps.GetAddressOf());
	if (FAILED(hRes))
	{
		printf("SMTC: Failed to get music properties!\n");
		return;
	}
	musicProps.As(&musicProp2);	// ComPtr stays NULL when casting should fail
	musicProps.As(&musicProp3);
	
	wchar_t* artistWStr = StrUTF8toUTF16(mInf->GetSongTagForDisp("AUTHOR"));
	wchar_t* titleWStr = StrUTF8toUTF16(mInf->GetSongTagForDisp("TITLE"));
	wchar_t* albumWStr = StrUTF8toUTF16(mInf->GetSongTagForDisp("GAME"));
	
	hRes = musicProps->put_Artist(MsWRL::Wrappers::HStringReference(artistWStr).Get());
	if (FAILED(hRes))
		printf("SMTC: Failed to set artist\n");
	hRes = musicProps->put_Title(MsWRL::Wrappers::HStringReference(titleWStr).Get());
	if (FAILED(hRes))
		printf("SMTC: Failed to set title\n");
	hRes = musicProps->put_AlbumArtist(MsWRL::Wrappers::HStringReference(albumWStr).Get());
	if (FAILED(hRes))
		printf("SMTC: Failed to set album\n");
	
	if (musicProp2)
		musicProp2->put_TrackNumber(1 + mInf->_playlistTrkID);
	if (musicProp3)
		musicProp3->put_AlbumTrackCount(mInf->_playlistTrkCnt);
	
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


static void CancelAsync(void)
{
	if (! mSFileAsyncOp)
		return;
	
	HRESULT hRes;
	IAsyncInfo* asyncInfo;
	hRes = mSFileAsyncOp->QueryInterface(IID_IAsyncInfo, reinterpret_cast<void**>(&asyncInfo));
	if (FAILED(hRes))
		return;
	asyncInfo->Cancel();
	mThumbPathWIP = std::string();
	
	return;
}

static void ClearThumbnail()
{
	if (mDispUpd == NULL)
		return;
	
	CancelAsync();
	mDispUpd->put_Thumbnail(nullptr);
	mLastThumbPath = std::string();
	mDispUpd->Update();
	
	return;
}

static HRESULT StorageFileAsyncCallback(WinFoundation::IAsyncOperation<WinStrg::StorageFile*>* aAsyncOp, AsyncStatus aStatus)
{
	if (aStatus != AsyncStatus::Completed)
		return E_ABORT;	// we only want completed operations
	
	HRESULT hRes;
	IAsyncInfo* asyncInfo;
	hRes = aAsyncOp->QueryInterface(IID_IAsyncInfo, reinterpret_cast<void**>(&asyncInfo));
	if (FAILED(hRes))
		return hRes;	// failed to get asyncInfo

	hRes = S_OK;
	asyncInfo->get_ErrorCode(&hRes);
	if (FAILED(hRes))
		return hRes;	// async operation failed
	
	MsWRL::ComPtr<WinStrg::IStorageFile> mThumbISF;
	hRes = aAsyncOp->GetResults(mThumbISF.GetAddressOf());
	if (FAILED(hRes))
		return hRes;	// async operation failed
	
	SetThumbnail_Exec(mThumbISF);
	return S_OK;
}

static void SetThumbnail(const std::string& filePath)
{
	if (mDispUpd == NULL)
		return;
	if (mLastThumbPath == filePath)
		return;
	
	if (filePath.empty())
	{
		ClearThumbnail();
		return;
	}
	
	CancelAsync();
	mThumbPathWIP = filePath;
	mLastThumbPath = std::string();
	
	wchar_t* thumbPathW = StrUTF8toUTF16(mThumbPathWIP.c_str());
	
	MsWRL::ComPtr<WinStrg::IStorageFileStatics> strgFileStatic;
	HRESULT hRes = WinFoundation::GetActivationFactory(
		MsWRL::Wrappers::HStringReference(RuntimeClass_WinStrg_StorageFile).Get(),
		strgFileStatic.GetAddressOf());
	
	// soooo much boilerplate code just to get the StorageFile object ...
	hRes = strgFileStatic->GetFileFromPathAsync(MsWRL::Wrappers::HStringReference(thumbPathW).Get(), &mSFileAsyncOp);
	free(thumbPathW);
	if (FAILED(hRes))
	{
		printf("SMTC: GetFileFromPathAsync failed!\n");
		return;
	}
	
	auto asyncHandler = MsWRL::Callback<WinFoundation::IAsyncOperationCompletedHandler<WinStrg::StorageFile*>>(StorageFileAsyncCallback);
	hRes = mSFileAsyncOp->put_Completed(asyncHandler.Get());
	if (FAILED(hRes))
		return;
	
	return;
}

static void SetThumbnail_Exec(MsWRL::ComPtr<WinStrg::IStorageFile> thumbISF)
{
	HRESULT hRes;
	MsWRL::ComPtr<WinStrgStrm::IRandomAccessStreamReferenceStatics> streamRefFactory;
	
	hRes = WinFoundation::GetActivationFactory(
		MsWRL::Wrappers::HStringReference(RuntimeClass_WinStrgStrm_RandAccStreamRef).Get(),
		streamRefFactory.GetAddressOf());
	if (FAILED(hRes))
	{
		printf("SMTC: Unable to create RandomAccessStreamReferenceStatics object!\n");
		return;
	}
	
	MsWRL::ComPtr<WinStrgStrm::IRandomAccessStreamReference> mImgStrmRef;
	hRes = streamRefFactory->CreateFromFile(thumbISF.Get(), mImgStrmRef.GetAddressOf());
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
	
	hRes = mDispUpd->Update();
	if (FAILED(hRes))
	{
		printf("SMTC: Failed to update thumbnail\n");
		return;
	}
	
	mLastThumbPath = mThumbPathWIP;
	mThumbPathWIP = std::string();
	
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
	
	//if (signalMask & MI_SIG_POSITION) ;
	//if (signalMask & MI_SIG_VOLUME) ;
	
	return;
}
