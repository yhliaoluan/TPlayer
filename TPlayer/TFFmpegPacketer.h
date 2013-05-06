#ifndef _T_FFMPEG_PACKETER_H_
#define _T_FFMPEG_PACKETER_H_

#include "TFFmpegDef.h"

class TFFmpegPacketer
{
public:
	TFFmpegPacketer(FFContext *);
	virtual ~TFFmpegPacketer();
	int Start();
	int GetPacket(FFPacketList **ppPkt);
	int GetFirstPktOpe(enum FFPktOpe *);
	int GetPacketCount();
	int FreeSinglePktList(FFPacketList **ppPkt);
	int Init();
	int SeekPos(int64_t pos);
	BOOL IsFinished();
private:
	TFF_Event _hEOFEvent;
	TFF_Thread _hThread;
	FFPacketQueue *_pQ;
	FFContext *_pCtx;
	BOOL _isFinished;

#define	PkterCmd_None  0x0000
#define	PkterCmd_Exit  0x0001
	int _cmd;

	//put 
	int PutIntoPktQueue(
		enum FFPktOpe opeType,
		void *pPkt);
	int ClearPktQueue();
	int DestroyPktQueue();
	void __stdcall ThreadStart();
	static unsigned long __stdcall SThreadStart(void *);
};

#endif