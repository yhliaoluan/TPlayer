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
	TFF_Mutex _hFrameListMutex;
	TFF_Event _hGetFrameEvent;
	TFF_Event _hEOFEvent;
	TFF_Thread _hThread;
	TFF_Event _hSyncEvent;
	FFFrameList *_pFrameList;
	FFContext *_pCtx;
	TFFmpegPacketer *_pPkter;
	BOOL _isFinished;

#define	DecoderCmd_None  0x0000
#define	DecoderCmd_Exit  0x0001
	int _cmd;

	//put 
	int PutIntoFrameList(
		AVFrame *pFrameRGB,
		uint8_t *pBuffer,
		enum FFFrameOpe,
		int lockMutex = 1);
	int PutIntoFrameList(AVPacket *pPkt, int64_t pdts);
	int Flush(FFPacketList *pPktList);

	void __stdcall ThreadStart();
	int FreeFrameList();
	int AllocRGBFrame(
		OUT AVFrame **ppFrameRGB,
		OUT uint8_t **ppBuffer);
	static unsigned long __stdcall SThreadStart(void *);
};
#endif