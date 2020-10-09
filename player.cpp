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
	_masterVol = 0x10000;	// fixed point 16.16
	_loopCount = 2;
	_fadeSmpls = 0;
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
	return _pbSpeed;
}

void PlayerWrapper::SetPlaybackSpeed(double speed)
{
	_pbSpeed = speed;
	if (_player != NULL)
		_player->SetPlaybackSpeed(_pbSpeed);
	return;
}

UINT32 PlayerWrapper::GetLoopCount(void) const
{
	return _loopCount;
}

void PlayerWrapper::SetLoopCount(UINT32 loops)
{
	_loopCount = loops;
	return;
}

UINT32 PlayerWrapper::GetFadeTime(void) const
{
	return _fadeSmpls;
}

void PlayerWrapper::SetFadeTime(UINT32 smplCnt)
{
	_fadeSmpls = smplCnt;
	return;
}

void PlayerWrapper::SetCallback(PLAYER_EVENT_CB cbFunc, void* cbParam)
{
	_plrCbFunc = cbFunc;
	_plrCbParam = cbParam;
	return;
}

UINT8 PlayerWrapper::GetState(void) const
{
	if (_player == NULL)
		return 0x00;
	UINT8 finalState = _myPlayState;
	if (_fadeSmplStart != (UINT32)-1)
		finalState |= PLAYSTATE_FADE;
	return _player->GetState();
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
		return _player->Tick2Second(_player->GetTotalPlayTicks(_loopCount));
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
	_player->SetCallback(PlayerWrapper::PlayCallbackS, this);
	
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
	_player->SetPlaybackSpeed(_pbSpeed);
	_fadeSmplStart = (UINT32)-1;
	
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
	return retVal;
}

UINT8 PlayerWrapper::Reset(void)
{
	if (_player == NULL)
		return 0xFF;
	_fadeSmplStart = (UINT32)-1;
	UINT8 retVal = _player->Reset();
	_myPlayState = _player->GetState() & (PLAYSTATE_PLAY | PLAYSTATE_END);
	return retVal;
}

UINT8 PlayerWrapper::FadeOut(void)
{
	if (_player == NULL)
		return 0xFF;
	_fadeSmplStart = _player->GetCurPos(PLAYPOS_SAMPLE);
	return 0x00;
}

UINT8 PlayerWrapper::Seek(UINT8 unit, UINT32 pos)
{
	if (_player == NULL)
		return 0xFF;
	_myPlayState = 0x00;
	UINT8 retVal = _player->Seek(unit, pos);
	if (_player->GetCurPos(PLAYPOS_SAMPLE) < _fadeSmplStart)
		_fadeSmplStart = (UINT32)-1;
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

UINT32 PlayerWrapper::CalcCurrentVolume(UINT32 playbackSmpl)
{
	UINT32 curVol;	// 16.16 fixed point
	
	// 1. master volume
	curVol = _masterVol;
	
	// 2. apply fade-out factor
	if (playbackSmpl >= _fadeSmplStart)
	{
		UINT32 fadeSmpls;
		UINT64 fadeVol;	// 64 bit for less type casts when doing multiplications with .16 fixed point
		
		fadeSmpls = playbackSmpl - _fadeSmplStart;
		if (fadeSmpls >= _fadeSmpls)
			return 0x0000;	// going beyond fade time -> volume 0
		
		fadeVol = (UINT64)fadeSmpls * 0x10000 / _fadeSmpls;
		fadeVol = 0x10000 - fadeVol;	// fade from full volume to silence
		fadeVol = fadeVol * fadeVol;	// logarithmic fading sounds nicer
		curVol = (UINT32)((fadeVol * curVol) >> 32);
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
	
	curVolume = (INT32)CalcCurrentVolume(basePbSmpl) >> VOL_SHIFT;
	SmplPtr16 = (INT16*)data;
	for (curSmpl = 0; curSmpl < smplCount; curSmpl ++, basePbSmpl ++, SmplPtr16 += 2)
	{
		if (basePbSmpl >= _fadeSmplStart)
		{
			UINT32 fadeSmpls = basePbSmpl - _fadeSmplStart;
			if (fadeSmpls >= _fadeSmpls && ! (_myPlayState & PLAYSTATE_END))
			{
				_myPlayState |= PLAYSTATE_END;
				if (_plrCbFunc != NULL)
					_plrCbFunc(_player, _plrCbParam, PLREVT_END, NULL);
				break;
			}
			
			curVolume = (INT32)CalcCurrentVolume(basePbSmpl) >> VOL_SHIFT;
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
	
	if (_plrCbFunc != NULL)
		retVal = _plrCbFunc(player, _plrCbParam, evtType, evtParam);
	if (retVal)
		return retVal;
	
	switch(evtType)
	{
	case PLREVT_START:
	case PLREVT_STOP:
		break;
	case PLREVT_LOOP:
		{
			UINT32* curLoop = (UINT32*)evtParam;
			if (_loopCount > 0 && *curLoop >= _loopCount)
			{
				if (_fadeSmpls > 0)
				{
					if (_fadeSmplStart == (UINT32)-1)
						_fadeSmplStart = player->GetCurPos(PLAYPOS_SAMPLE);
				}
				else
				{
					_myPlayState |= PLAYSTATE_END;
					return 0x01;
				}
			}
		}
		break;
	case PLREVT_END:
		_myPlayState |= PLAYSTATE_END;
		break;
	}
	return 0x00;
}
