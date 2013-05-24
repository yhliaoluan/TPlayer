#ifndef _TFF_MPEG_CLOCK_H_
#define _TFF_MPEG_CLOCK_H_

class TFFmpegClock
{
public:
	TFFmpegClock();
	~TFFmpegClock();
	void Start();
	long GetTime();
	void Pause();
	void Stop();
	void Sync(long ms);
private:
	long _ms;
	long _lastRecordedMS;

	enum
	{
		CLOCK_STATE_STARTED,
		CLOCK_STATE_PAUSED,
		CLOCK_STATE_STOPPED
	};

	int _state;

	inline void Tick();
};

#endif