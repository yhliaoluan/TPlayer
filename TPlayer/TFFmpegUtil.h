#ifndef _T_FFMPEG_UTIL_H_
#define _T_FFMPEG_UTIL_H_

#include "TFFmpegPlatform.h"

#define FF_DEBUG_OUTPUT

static void inline
DebugOutput(const char *msg)
{
#ifdef FF_DEBUG_OUTPUT
	TFF_OutputDebugString(msg);
#endif
}

static inline void
CloseThreadP(TFF_Thread *thread)
{
	if(*thread)
	{
		TFF_CloseThread(*thread);
		*thread = NULL;
	}
}

static inline void
CloseMutexP(TFF_Mutex *mutex)
{
	if(*mutex)
	{
		TFF_CloseMutex(*mutex);
		*mutex = NULL;
	}
}

static inline void
CloseEventP(TFF_Event *event)
{
	if(*event)
	{
		TFF_CloseEvent(*event);
		*event = NULL;
	}
}

#endif