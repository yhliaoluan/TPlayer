#ifndef _T_FFMPEG_VIDEO_DECODER_H_
#define _T_FFMPEG_VIDEO_DECODER_H_

#include "TFFmpegDef.h"
#include "TFFmpegPacketer.h"

class TFFmpegVideoDecoder
{
public:
	TFFmpegVideoDecoder(FFContext *, TFFmpegPacketer *pPkter);
	virtual ~TFFmpegVideoDecoder();
	int Start();
	//Get and remove first frame.
	int Pop(FFFrameList **);
	//Get but not remove first frame.
	int Get(FFFrameList **);
	int GetFrameCount();
	int Init();
	int FreeSingleFrameList(FFFrameList **);
	int SeekPos(int64_t pos);
	BOOL IsFinished();
	int SetResolution(int width, int height);
private:
	TFF_Event _hEOFEvent;
	TFF_Thread _hThread;
	TFF_Mutex _mutex;
	FFFrameQueue *_pVQ;
	FFContext *_pCtx;
	TFFmpegPacketer *_pPkter;
	BOOL _isFinished;
	SwsContext *_swsCtx;

#define	DecoderCmd_None  0x0000
#define	DecoderCmd_Exit  0x0001
#define DecoderCmd_Abandon 0x0002
	int _cmd;

	//put 
	int PutIntoFrameList(FFFrameList *);
	int PutIntoFrameList(AVFrame *pDecVFrame, int64_t pktDts, int64_t pdts);
	int Flush(int64_t pos, BOOL seekToPos = FALSE);

	void __stdcall ThreadStart();
	int ClearFrameQueue();
	int DestroyFrameQueue();
	int AllocDstFrame(OUT FFFrameList **);
	static unsigned long __stdcall SThreadStart(void *);
};
#endif