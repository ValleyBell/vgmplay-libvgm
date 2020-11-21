#include <vector>
#include "stdtype.h"
#include <utils/DataLoader.h>
#include <audio/AudioStream.h>
#include <player/playerbase.hpp>

#define PLAYSTATE_FADE	0x10	// is fading

struct PlrWrapConfig
{
	INT32 masterVol;	// master volume (16.16 fixed point, negative value = phase inversion)
	UINT8 chnInvert;	// channel phase inversion (bit 0 - left, bit 1 - right)
	UINT32 loopCount;
	UINT32 fadeSmpls;
	double pbSpeed;
};

class PlayerWrapper
{
public:
	PlayerWrapper();
	~PlayerWrapper();
	void RegisterPlayerEngine(PlayerBase* player);
	void UnregisterAllPlayers(void);
	const std::vector<PlayerBase*>& GetRegisteredPlayers(void) const;
	
	void PrepareRendering(const AUDIO_OPTS* opts, UINT32 bufSmpls);
	UINT32 GetSampleRate(void) const;
	void SetSampleRate(UINT32 sampleRate);
	double GetPlaybackSpeed(void) const;
	void SetPlaybackSpeed(double speed);
	UINT32 GetLoopCount(void) const;
	void SetLoopCount(UINT32 loops);
	UINT32 GetFadeTime(void) const;
	void SetFadeTime(UINT32 smplCnt);
	const PlrWrapConfig& GetConfiguration(void) const;
	void SetConfiguration(const PlrWrapConfig& config);

	void SetCallback(PLAYER_EVENT_CB cbFunc, void* cbParam);
	UINT8 GetState(void) const;
	UINT32 GetCurPos(UINT8 unit) const;
	double GetCurTime(UINT8 includeLoops) const;
	double GetTotalTime(UINT8 includeLoops) const;
	UINT32 GetCurLoop(void) const;
	double GetLoopTime(void) const;
	PlayerBase* GetPlayer(void);
	
	UINT8 LoadFile(DATA_LOADER* dLoad);
	UINT8 UnloadFile(void);
	UINT8 Start(void);
	UINT8 Stop(void);
	UINT8 Reset(void);
	UINT8 FadeOut(void);
	UINT8 Seek(UINT8 unit, UINT32 pos);
	UINT32 Render(UINT32 bufSize, void* data);
private:
	void FindPlayerEngine(void);
	INT32 CalcCurrentVolume(UINT32 playbackSmpl);
	UINT32 FillBuffer(void* drvStruct, void* userParam, UINT32 bufSize, void* data);
	static UINT8 PlayCallbackS(PlayerBase* player, void* userParam, UINT8 evtType, void* evtParam);
	UINT8 PlayCallback(PlayerBase* player, UINT8 evtType, void* evtParam);
	
	std::vector<PlayerBase*> _avbPlrs;	// available players
	UINT32 _smplRate;
	PlrWrapConfig _config;
	PLAYER_EVENT_CB _plrCbFunc;
	void* _plrCbParam;
	UINT8 _myPlayState;
	
	UINT8 _outSmplChns;
	UINT8 _outSmplBits;
	UINT32 _outSmplSize;
	std::vector<WAVE_32BS> _smplBuf;
	PlayerBase* _player;
	DATA_LOADER* _dLoad;
	UINT32 _fadeSmplStart;
};

