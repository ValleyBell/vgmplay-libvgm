#include <string.h>
#include <vector>

#include "stdtype.h"
#include <common_def.h>
#include <utils/DataLoader.h>
#include <audio/AudioStream.h>
#include <player/playerbase.hpp>
#include <emu/Resampler.h>

#include "player.hpp"

PlayerWrapper::PlayerWrapper()
{
	_config.masterVol = 0x10000;	// fixed point 16.16
	_config.chnInvert = 0x00;
	_config.loopCount = 2;
	_config.fadeSmpls = 0;
	_config.endSilenceSmpls = 0;
	_config.pbSpeed = 1.0;
	return;
}

PlayerWrapper::~PlayerWrapper()
{
	Stop();
	UnloadFile();
	UnregisterAllPlayers();
	return;
}

void PlayerWrapper::RegisterPlayerEngine(PlayerBase* player)
{
	_avbPlrs.push_back(player);
	return;
}

void PlayerWrapper::UnregisterAllPlayers(void)
{
	for (size_t curPlr = 0; curPlr < _avbPlrs.size(); curPlr ++)
		delete _avbPlrs[curPlr];
	_avbPlrs.clear();
	return;
}

const std::vector<PlayerBase*>& PlayerWrapper::GetRegisteredPlayers(void) const
{
	return _avbPlrs;
}

void PlayerWrapper::PrepareRendering(const AUDIO_OPTS* opts, UINT32 bufSmpls)
{
	_outSmplChns = opts->numChannels;
	_outSmplBits = opts->numBitsPerSmpl;
	_smplRate = opts->sampleRate;
	_outSmplSize = _outSmplChns * _outSmplBits / 8;
	_smplBuf.resize(bufSmpls);
	return;
}

UINT32 PlayerWrapper::GetSampleRate(void) const
{
	return _smplRate;
}

void PlayerWrapper::SetSampleRate(UINT32 sampleRate)
{
	_smplRate = sampleRate;
	return;
}

double PlayerWrapper::GetPlaybackSpeed(void) const
{
	return _config.pbSpeed;
}

void PlayerWrapper::SetPlaybackSpeed(double speed)
{
	_config.pbSpeed = speed;
	if (_player != NULL)
		_player->SetPlaybackSpeed(_config.pbSpeed);
	return;
}

UINT32 PlayerWrapper::GetLoopCount(void) const
{
	return _config.loopCount;
}

void PlayerWrapper::SetLoopCount(UINT32 loops)
{
	_config.loopCount = loops;
	return;
}

UINT32 PlayerWrapper::GetFadeSamples(void) const
{
	return _config.fadeSmpls;
}

void PlayerWrapper::SetFadeSamples(UINT32 smplCnt)
{
	_config.fadeSmpls = smplCnt;
	return;
}

UINT32 PlayerWrapper::GetEndSilenceSamples(void) const
{
	return _config.endSilenceSmpls;
}

void PlayerWrapper::SetEndSilenceSamples(UINT32 smplCnt)
{
	_config.endSilenceSmpls = smplCnt;
	return;
}

const PlrWrapConfig& PlayerWrapper::GetConfiguration(void) const
{
	return _config;
}

void PlayerWrapper::SetConfiguration(const PlrWrapConfig& config)
{
	double oldPbSpeed = _config.pbSpeed;
	
	_config = config;
	if (_player != NULL && oldPbSpeed != _config.pbSpeed)
		_player->SetPlaybackSpeed(_config.pbSpeed);
	return;
}

void PlayerWrapper::SetEventCallback(PLAYER_EVENT_CB cbFunc, void* cbParam)
{
	_plrCbFunc = cbFunc;
	_plrCbParam = cbParam;
	return;
}

void PlayerWrapper::SetFileReqCallback(PLAYER_FILEREQ_CB cbFunc, void* cbParam)
{
	for (size_t curPlr = 0; curPlr < _avbPlrs.size(); curPlr ++)
		_avbPlrs[curPlr]->SetFileReqCallback(cbFunc, cbParam);
	return;
}

UINT8 PlayerWrapper::GetState(void) const
{
	if (_player == NULL)
		return 0x00;
	UINT8 finalState = _myPlayState;
	if (_fadeSmplStart != (UINT32)-1)
		finalState |= PLAYSTATE_FADE;
	return finalState;
}

UINT32 PlayerWrapper::GetCurPos(UINT8 unit) const
{
	if (_player == NULL)
		return (UINT32)-1;
	return _player->GetCurPos(unit);
}

double PlayerWrapper::GetCurTime(UINT8 includeLoops) const
{
	if (_player == NULL)
		return -1.0;
	// using samples here, as it may be more accurate than the (possibly low-resolution) ticks
	double ticks = _player->Sample2Second(_player->GetCurPos(PLAYPOS_SAMPLE));
	if (! includeLoops)
	{
		UINT32 curLoop = _player->GetCurLoop();
		if (curLoop > 0)
			ticks -= _player->Tick2Second(_player->GetLoopTicks() * curLoop);
	}
	return ticks;
}

double PlayerWrapper::GetTotalTime(UINT8 includeLoops) const
{
	if (_player == NULL)
		return -1.0;
	if (includeLoops)
		return _player->Tick2Second(_player->GetTotalPlayTicks(_config.loopCount));
	else
		return _player->Tick2Second(_player->GetTotalPlayTicks(1));
}

UINT32 PlayerWrapper::GetCurLoop(void) const
{
	if (_player == NULL)
		return (UINT32)-1;
	return _player->GetCurLoop();
}

double PlayerWrapper::GetLoopTime(void) const
{
	if (_player == NULL)
		return -1.0;
	return _player->Tick2Second(_player->GetLoopTicks());
}

PlayerBase* PlayerWrapper::GetPlayer(void)
{
	return _player;
}

void PlayerWrapper::FindPlayerEngine(void)
{
	size_t curPlr;
	
	_player = NULL;
	for (curPlr = 0; curPlr < _avbPlrs.size(); curPlr ++)
	{
		UINT8 retVal = _avbPlrs[curPlr]->CanLoadFile(_dLoad);
		if (! retVal)
		{
			_player = _avbPlrs[curPlr];
			return;
		}
	}
	
	return;
}

UINT8 PlayerWrapper::LoadFile(DATA_LOADER* dLoad)
{
	_dLoad = dLoad;
	FindPlayerEngine();
	if (_player == NULL)
		return 0xFF;
	_player->SetEventCallback(PlayerWrapper::PlayCallbackS, this);
	
	UINT8 retVal = _player->LoadFile(dLoad);
	if (retVal >= 0x80)
		return retVal;
	return retVal;
}

UINT8 PlayerWrapper::UnloadFile(void)
{
	if (_player == NULL)
		return 0xFF;
	
	_player->Stop();
	UINT8 retVal = _player->UnloadFile();
	_player = NULL;
	return retVal;
}

UINT8 PlayerWrapper::Start(void)
{
	if (_player == NULL)
		return 0xFF;
	_player->SetSampleRate(_smplRate);
	_player->SetPlaybackSpeed(_config.pbSpeed);
	_fadeSmplStart = (UINT32)-1;
	_endSilenceStart = (UINT32)-1;
	
	UINT8 retVal = _player->Start();
	_myPlayState = _player->GetState() & (PLAYSTATE_PLAY | PLAYSTATE_END);
	return retVal;
}

UINT8 PlayerWrapper::Stop(void)
{
	if (_player == NULL)
		return 0xFF;
	UINT8 retVal = _player->Stop();
	_myPlayState = _player->GetState() & (PLAYSTATE_PLAY | PLAYSTATE_END);
	_myPlayState |= PLAYSTATE_FIN;
	return retVal;
}

UINT8 PlayerWrapper::Reset(void)
{
	if (_player == NULL)
		return 0xFF;
	_fadeSmplStart = (UINT32)-1;
	_endSilenceStart = (UINT32)-1;
	UINT8 retVal = _player->Reset();
	_myPlayState = _player->GetState() & (PLAYSTATE_PLAY | PLAYSTATE_END);
	return retVal;
}

UINT8 PlayerWrapper::FadeOut(void)
{
	if (_player == NULL)
		return 0xFF;
	if (_fadeSmplStart == (UINT32)-1)
		_fadeSmplStart = _player->GetCurPos(PLAYPOS_SAMPLE);
	return 0x00;
}

UINT8 PlayerWrapper::Seek(UINT8 unit, UINT32 pos)
{
	if (_player == NULL)
		return 0xFF;
	UINT8 retVal = _player->Seek(unit, pos);
	_myPlayState = _player->GetState() & (PLAYSTATE_PLAY | PLAYSTATE_END);
	
	UINT32 pbSmpl = _player->GetCurPos(PLAYPOS_SAMPLE);
	if (pbSmpl < _fadeSmplStart)
		_fadeSmplStart = (UINT32)-1;
	if (pbSmpl < _endSilenceStart)
		_endSilenceStart = (UINT32)-1;
	return retVal;
}

#if 1
#define VOLCALC64
#define VOL_BITS	16	// use .X fixed point for working volume
#else
#define VOL_BITS	8	// use .X fixed point for working volume
#endif
#define VOL_SHIFT	(16 - VOL_BITS)	// shift for master volume -> working volume

// Pre- and post-shifts are used to make the calculations as accurate as possible
// without causing the sample data (likely 24 bits) to overflow while applying the volume gain.
// Smaller values for VOL_PRESH are more accurate, but have a higher risk of overflows during calculations.
// (24 + VOL_POSTSH) must NOT be larger than 31
#define VOL_PRESH	4	// sample data pre-shift
#define VOL_POSTSH	(VOL_BITS - VOL_PRESH)	// post-shift after volume multiplication

INT32 PlayerWrapper::CalcCurrentVolume(UINT32 playbackSmpl)
{
	INT32 curVol;	// 16.16 fixed point
	
	// 1. master volume
	curVol = _config.masterVol;
	
	// 2. apply fade-out factor
	if (playbackSmpl >= _fadeSmplStart)
	{
		UINT32 fadeSmpls;
		UINT64 fadeVol;	// 64 bit for less type casts when doing multiplications with .16 fixed point
		
		fadeSmpls = playbackSmpl - _fadeSmplStart;
		if (fadeSmpls >= _config.fadeSmpls)
			return 0x0000;	// going beyond fade time -> volume 0
		
		fadeVol = (UINT64)fadeSmpls * 0x10000 / _config.fadeSmpls;
		fadeVol = 0x10000 - fadeVol;	// fade from full volume to silence
		fadeVol = fadeVol * fadeVol;	// logarithmic fading sounds nicer
		curVol = (INT32)(((INT64)fadeVol * curVol) >> 32);
	}
	
	return curVol;
}

UINT32 PlayerWrapper::Render(UINT32 bufSize, void* data)
{
	UINT32 basePbSmpl;
	UINT32 smplCount;
	UINT32 smplRendered;
	INT16* SmplPtr16;
	UINT32 curSmpl;
	WAVE_32BS fnlSmpl;	// final sample value
	INT32 curVolume;
	
	smplCount = bufSize / _outSmplSize;
	if (! smplCount)
		return 0;
	
	if (_player == NULL)
	{
		memset(data, 0x00, smplCount * _outSmplSize);
		return smplCount * _outSmplSize;
	}
	if (! (_player->GetState() & PLAYSTATE_PLAY))
	{
		//fprintf(stderr, "Player Warning: calling Render while not playing! playState = 0x%02X\n", _player->GetState());
		memset(data, 0x00, smplCount * _outSmplSize);
		return smplCount * _outSmplSize;
	}
	
	if (smplCount > _smplBuf.size())
		smplCount = _smplBuf.size();
	memset(&_smplBuf[0], 0, smplCount * sizeof(WAVE_32BS));
	basePbSmpl = _player->GetCurPos(PLAYPOS_SAMPLE);
	smplRendered = _player->Render(smplCount, &_smplBuf[0]);
	smplCount = smplRendered;
	
	curVolume = CalcCurrentVolume(basePbSmpl) >> VOL_SHIFT;
	SmplPtr16 = (INT16*)data;
	for (curSmpl = 0; curSmpl < smplCount; curSmpl ++, basePbSmpl ++, SmplPtr16 += 2)
	{
		if (basePbSmpl >= _fadeSmplStart)
		{
			UINT32 fadeSmpls = basePbSmpl - _fadeSmplStart;
			if (fadeSmpls >= _config.fadeSmpls && ! (_myPlayState & PLAYSTATE_END))
			{
				if (_endSilenceStart == (UINT32)-1)
					_endSilenceStart = basePbSmpl;
				_myPlayState |= PLAYSTATE_END;
			}
			
			curVolume = CalcCurrentVolume(basePbSmpl) >> VOL_SHIFT;
		}
		if (basePbSmpl >= _endSilenceStart)
		{
			UINT32 silenceSmpls = basePbSmpl - _endSilenceStart;
			if (silenceSmpls >= _config.endSilenceSmpls && ! (_myPlayState & PLAYSTATE_FIN))
			{
				_myPlayState |= PLAYSTATE_FIN;
				if (_plrCbFunc != NULL)
					_plrCbFunc(_player, _plrCbParam, PLREVT_END, NULL);
				break;
			}
		}
		
		// Input is about 24 bits (some cores might output a bit more)
		fnlSmpl = _smplBuf[curSmpl];
		
#ifdef VOLCALC64
		fnlSmpl.L = (INT32)( ((INT64)fnlSmpl.L * curVolume) >> VOL_BITS );
		fnlSmpl.R = (INT32)( ((INT64)fnlSmpl.R * curVolume) >> VOL_BITS );
#else
		fnlSmpl.L = ((fnlSmpl.L >> VOL_PRESH) * curVolume) >> VOL_POSTSH;
		fnlSmpl.R = ((fnlSmpl.R >> VOL_PRESH) * curVolume) >> VOL_POSTSH;
#endif
		
		if (_config.chnInvert & 0x01)
			fnlSmpl.L = -fnlSmpl.L;
		if (_config.chnInvert & 0x02)
			fnlSmpl.R = -fnlSmpl.R;
		
		fnlSmpl.L >>= 8;	// 24 bit -> 16 bit
		fnlSmpl.R >>= 8;
		if (fnlSmpl.L < -0x8000)
			fnlSmpl.L = -0x8000;
		else if (fnlSmpl.L > +0x7FFF)
			fnlSmpl.L = +0x7FFF;
		if (fnlSmpl.R < -0x8000)
			fnlSmpl.R = -0x8000;
		else if (fnlSmpl.R > +0x7FFF)
			fnlSmpl.R = +0x7FFF;
		SmplPtr16[0] = (INT16)fnlSmpl.L;
		SmplPtr16[1] = (INT16)fnlSmpl.R;
	}
	
	return curSmpl * _outSmplSize;
}

/*static*/ UINT8 PlayerWrapper::PlayCallbackS(PlayerBase* player, void* userParam, UINT8 evtType, void* evtParam)
{
	PlayerWrapper* plr = (PlayerWrapper*)userParam;
	return plr->PlayCallback(player, evtType, evtParam);
}

UINT8 PlayerWrapper::PlayCallback(PlayerBase* player, UINT8 evtType, void* evtParam)
{
	UINT8 retVal = 0x00;
	
	if (evtType != PLREVT_END)	// We will generate our own PLREVT_END event depending on fading/endSilence.
	{
		if (_plrCbFunc != NULL)
			retVal = _plrCbFunc(player, _plrCbParam, evtType, evtParam);
		if (retVal)
			return retVal;
	}
	
	switch(evtType)
	{
	case PLREVT_START:
	case PLREVT_STOP:
		break;
	case PLREVT_LOOP:
		{
			UINT32* curLoop = (UINT32*)evtParam;
			if (_config.loopCount > 0 && *curLoop >= _config.loopCount)
			{
				//if (_config.fadeSmpls == 0)
				//	return 0x01;	// send "stop" signal to player engine
				FadeOut();
			}
		}
		break;
	case PLREVT_END:
		_myPlayState |= PLAYSTATE_END;
		_endSilenceStart = player->GetCurPos(PLAYPOS_SAMPLE);
		break;
	}
	return 0x00;
}
