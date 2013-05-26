#ifndef _T_FFMPEG_PACKETER_H_
#define _T_FFMPEG_PACKETER_H_

#include "TFFmpegDef.h"

class TFFmpegPacketer
{
public:
	TFFmpegPacketer(FFContext *);
	virtual ~TFFmpegPacketer();
	int Start();
	int GetVideoPacket(FFPacketList **pkt);
	int GetAudioPacket(FFPacketList **pkt);
	int GetSubtitlePacket(FFPacketList **pkt);
	int FreeSinglePktList(FFPacketList **pkt);
	int Init();
	int SeekPos(double);
private:
	TFF_Thread _thread;
	TFF_Cond _readCond;
	TFF_Mutex _readMutex;
	FFPacketQueue *_videoQ;
	FFPacketQueue *_audioQ;
	FFPacketQueue *_subtitleQ;
	FFContext *_ctx;
	BOOL _isFinished;

#define PKT_Q_VIDEO			1
#define PKT_Q_AUDIO			2
#define PKT_Q_SUBTITLE		3

#define	PkterCmd_None  0x0000
#define	PkterCmd_Exit  0x0001
	int _cmd;

	//put 
	int PutIntoPktQueue(
		FFPacketQueue *q,
		AVPacket *pkt);
	int GetPacket(FFPacketQueue *, FFPacketList **);
	int ClearPktQueue(FFPacketQueue *q);
	int DestroyPktQueue(FFPacketQueue **);
	int InitPacketQueue(FFPacketQueue **, int type);
	void __stdcall ThreadStart();
	static unsigned long __stdcall SThreadStart(void *);
};

#endif