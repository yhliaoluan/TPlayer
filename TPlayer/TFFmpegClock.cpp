#include "TFFmpegClock.h"
#include "TFFmpegUtil.h"
#include <time.h>

TFFmpegClock::TFFmpegClock() :
	_ms(0),
	_lastRecordedMS(0),
	_state(CLOCK_STATE_STOPPED)
{
}

TFFmpegClock::~TFFmpegClock()
{
}

void TFFmpegClock::Start()
{
	if (_state == CLOCK_STATE_STARTED)
		return;
	_lastRecordedMS = clock();
	_state = CLOCK_STATE_STARTED;
}

void TFFmpegClock::Pause()
{
	if (_state == CLOCK_STATE_STARTED)
	{
		Tick();
		_state = CLOCK_STATE_PAUSED;
	}
}

void TFFmpegClock::Stop()
{
	_ms = 0;
	_lastRecordedMS = 0;
	_state = CLOCK_STATE_STOPPED;
}

void TFFmpegClock::Sync(long externalMS)
{
	Tick();
	if (externalMS > 0 && abs(_ms - externalMS) > 20)
	{
		TFFLog(TFF_LOG_LEVEL_DEBUG, "Sync external clock from %d to %d", _ms, externalMS);
		_ms = externalMS;
	}
}

long TFFmpegClock::GetTime()
{
	if (_state == CLOCK_STATE_STARTED)
		Tick();
	return _ms;
}

void TFFmpegClock::Tick()
{
	long ms = clock();
	_ms += ms - _lastRecordedMS;
	_lastRecordedMS = ms;
}