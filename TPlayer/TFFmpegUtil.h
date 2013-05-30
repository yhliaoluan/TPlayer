#ifndef _T_FFMPEG_UTIL_H_
#define _T_FFMPEG_UTIL_H_

#include "TFFmpegPlatform.h"
#include "TFFmpegDef.h"
#include "log.h"

#define FF_DEBUG_OUTPUT

static inline void
CloseThreadP(TFF_Thread *thread)
{
	if (*thread)
	{
		TFF_CloseThread(*thread);
		*thread = NULL;
	}
}

static inline void
CloseMutexP(TFF_Mutex *mutex)
{
	if (*mutex)
	{
		TFF_CloseMutex(*mutex);
		*mutex = NULL;
	}
}

static inline void
CloseEventP(TFF_Event *event)
{
	if (*event)
	{
		TFF_CloseEvent(*event);
		*event = NULL;
	}
}

inline enum AVPixelFormat
FF_GetAVPixFmt(int ffFmt)
{
	switch(ffFmt)
	{
	case FF_FRAME_PIXFORMAT_YUV420:
		return AV_PIX_FMT_YUV420P;
	case FF_FRAME_PIXFORMAT_RGB32:
		return AV_PIX_FMT_BGR0;
	default:
		return PIX_FMT_BGR24;
	}
}

inline enum AVSampleFormat
FF_GetAVSampleFmt(int fmt)
{
	return AV_SAMPLE_FMT_S16;
}

#endif