#ifndef _T_FFMPEG_DEF_H_
#define _T_FFMPEG_DEF_H_

#include "TFFmpegPlatform.h"
extern "C"
{
#include <libavformat\avformat.h>
#include <libswscale\swscale.h>
}

enum FFFrameOpe { FrameOpe_None };

enum FFCmd
{
	FFCmd_None,
	FFCmd_Run,
	FFCmd_Pause,
	FFCmd_Stop,
	FFCmd_Exit,
	FFCmd_Step,
	FFCmd_Seek,
};

typedef struct _st_FFPacketList
{
	AVPacket *pPkt;
	struct _st_FFPacketList *next;
} FFPacketList;

typedef struct _st_FFPacketQueue
{
	TFF_Mutex mutex;
	TFF_Cond cond;
	size_t count;
	size_t size;
	int type;
	FFPacketList *first, *last;
} FFPacketQueue;

typedef struct _st_FFSeekPosPkt
{
	int64_t pos;
} FFSeekPosPkt;

typedef struct _st_FFFrameList
{
	AVFrame *pFrame;
	uint8_t *buffer;
	enum FFFrameOpe ope;
	int width;
	int height;
	size_t size;
	struct _st_FFFrameList *next;
} FFFrameList;

typedef struct
{
	TFF_Mutex mutex;
	TFF_Cond cond;
	size_t count;
	size_t size;
	FFFrameList *first, *last;
} FFFrameQueue;

typedef struct _st_FFSettings
{
	int width;
	int height;
	int fpsNum;
	int fpsDen;
	int timebaseNum;
	int timebaseDen;
	int audioSampleRate;
	int audioChannels;
	long long duration;
	long long totalFrames;
	char codecName[128];
} FFSettings;

#define FF_FRAME_PIXFORMAT_RGB24 0
#define FF_FRAME_PIXFORMAT_YUV420 1
#define FF_FRAME_PIXFORMAT_RGB32 2

typedef struct _st_FFInitSetting
{
	wchar_t fileName[260];
	int dstFramePixFmt;
} FFInitSetting;

typedef struct _st_FFFrame
{
	unsigned char **data;
	int *linesize;
	unsigned char *buff;
	int keyFrame;
	long long dts;
	int size;
	double time;
	int width;
	int height;
} FFFrame;

typedef void (__stdcall *NewFrameCB)(FFFrame *p);
typedef void (__stdcall *FinishedCB)(void);

typedef struct _st_FFContext
{
	AVFormatContext *pFmtCtx;
	AVStream *videoStream;
	AVStream *audioStream;
	int videoStreamIdx;
	int audioStreamIdx;
	int handleVideo;
	int handleAudio;
	enum AVPixelFormat dstPixFmt;
	int dstWidth;
	int dstHeight;
} FFContext;

#define FF_OK								0
#define FF_EOF								2
#define FF_NO_AUDIO_STREAM					3

//co ffmpeg errors
#define FF_ERR_GENERAL						-100
#define FF_ERR_CANNOT_OPEN_FILE				(FF_ERR_GENERAL - 1)
#define FF_ERR_CANNOT_FIND_STREAM_INFO		(FF_ERR_GENERAL - 2)
#define FF_ERR_NO_VIDEO_STREAM				(FF_ERR_GENERAL - 3)
#define FF_ERR_CANNOT_OPEN_VIDEO_CODEC		(FF_ERR_GENERAL - 4)
#define FF_ERR_CANNOT_OPEN_AUDIO_CODEC		(FF_ERR_GENERAL - 5)
#define FF_ERR_NOPOINTER					(FF_ERR_GENERAL - 6)

#endif