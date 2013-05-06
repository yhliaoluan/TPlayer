#ifndef _T_FFMPEG_DECODER_H_
#define _T_FFMPEG_DECODER_H_

#include "TFFmpegDef.h"
#include "TFFmpegPacketer.h"

class TFFmpegDecoder
{
public:
	TFFmpegDecoder(FFContext *, TFFmpegPacketer *pPkter);
	virtual ~TFFmpegDecoder();
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
private:
	TFF_Event _hEOFEvent;
	TFF_Thread _hThread;
	TFF_Mutex _mutex;
	FFFrameQueue *_pQ;
	FFContext *_pCtx;
	TFFmpegPacketer *_pPkter;
	BOOL _isFinished;

#define	DecoderCmd_None  0x0000
#define	DecoderCmd_Exit  0x0001
#define DecoderCmd_Abandon 0x0002
	int _cmd;

	//put 
	int PutIntoFrameList(
		AVFrame *pFrameRGB,
		uint8_t *pBuffer,
		enum FFFrameOpe);
	int PutIntoFrameList(AVPacket *pPkt, int64_t pdts);
	int Flush(int64_t pos, BOOL seekToPos = FALSE);

	void __stdcall ThreadStart();
	int ClearFrameQueue();
	int DestroyFrameQueue();
	int AllocRGBFrame(
		OUT AVFrame **ppFrameRGB,
		OUT uint8_t **ppBuffer);
	static unsigned long __stdcall SThreadStart(void *);
};
#endif