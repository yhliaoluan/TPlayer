#ifndef _T_FFMPEG_PACKETER_H_
#define _T_FFMPEG_PACKETER_H_

#include "TFFmpegDef.h"

class TFFmpegPacketer
{
public:
	TFFmpegPacketer(FFContext *);
	virtual ~TFFmpegPacketer();
	int Start();
	int GetPacket(FFPacketList **ppPktList);
	int GetFirstPktOpe(enum FFPktOpe *);
	int GetPacketCount();
	int FreeSinglePktList(FFPacketList **);
	int Init();
	int SeekPos(int64_t pos);
	BOOL IsFinished();
private:
	TFF_Mutex _hPktListMutex;
	TFF_Event _hGetPktEvent;
	TFF_Event _hEOFEvent;
	TFF_Thread _hThread;
	TFF_Event _hSeekEvent;
	FFPacketList *_pPktList;
	FFContext *_pCtx;
	BOOL _isFinished;

#define	PkterCmd_None  0x0000
#define	PkterCmd_Seek  0x0001
#define	PkterCmd_Exit  0x0002
	int _cmd;

	int64_t _pos;

	//put 
	int PutIntoPktList(
		enum FFPktOpe opeType,
		void *pPkt,
		int lockMutex = 1);
	int FreePktList();
	void __stdcall ThreadStart();
	static unsigned long __stdcall SThreadStart(void *);
};

#endif