#ifndef _T_FFMPEG_DEF_H_
#define _T_FFMPEG_DEF_H_

#include "TFFmpegPlatform.h"
extern "C"
{
#include <libavformat\avformat.h>
#include <libswscale\swscale.h>
}

enum FFPktOpe { PktOpe_None, PktOpe_Flush };

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
	void *pPkt;
	enum FFPktOpe ope;
	struct _st_FFPacketList *next;
} FFPacketList;

typedef struct _st_FFPacketQueue
{
	TFF_Mutex mutex;
	TFF_Cond cond;
	size_t count;
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
	struct _st_FFFrameList *next;
} FFFrameList;

typedef struct
{
	TFF_Mutex mutex;
	TFF_Cond cond;
	size_t count;
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
	long long duration;
	long long totalFrames;
	char codecName[128];
} FFSettings;

typedef struct _st_FFFrame
{
	unsigned char *buff;
	long long pos;
	int keyFrame;
	long long bets; //best effort timestamp
	long long pts;
	long long dts;
	int oriSize;
	int size;
	double time;
} FFFrame;

typedef void (__stdcall *NewFrameCB)(FFFrame *p);
typedef void (__stdcall *FinishedCB)(void);

typedef struct _st_FFContext
{
	char fileName[260];
	AVFormatContext *pFmtCtx;
	AVStream *pVideoStream;
	SwsContext *pSwsCtx;
	FFFrame *pCurFrame;//Store the current rgb24 frame. Will be used in callback func.
	int videoStreamIdx;
	AVFrame *pDecodedFrame;
} FFContext;

#define FF_EOF								2

//co ffmpeg errors
#define FF_ERR_GENERAL						-100
#define FF_ERR_CANNOT_OPEN_FILE				-101
#define FF_ERR_CANNOT_FIND_STREAM_INFO		-102
#define FF_ERR_NO_VIDEO_STREAM				-103
#define FF_ERR_CANNOT_OPEN_CODEC			-104

#endif