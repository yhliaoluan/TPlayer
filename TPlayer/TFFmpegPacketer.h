#ifndef _T_FFMPEG_PACKETER_H_
#define _T_FFMPEG_PACKETER_H_

#include "TFFmpegDef.h"

class TFFmpegPacketer
{
public:
	TFFmpegPacketer(FF_CONTEXT *);
	virtual ~TFFmpegPacketer();
	int Start();
	int GetVideoPacket(FF_PACKET_LIST **pkt);
	int GetAudioPacket(FF_PACKET_LIST **pkt);
	int GetSubtitlePacket(FF_PACKET_LIST **pkt);
	int FreeSinglePktList(FF_PACKET_LIST **pkt);
	int Init();
	int SeekPos(double);
private:
	TFF_Thread _thread;
	TFF_Cond _readCond;
	TFF_Mutex _readMutex;
	FF_PACKET_QUEUE *_videoQ;
	FF_PACKET_QUEUE *_audioQ;
	FF_PACKET_QUEUE *_subtitleQ;
	FF_CONTEXT *_ctx;
	BOOL _isFinished;

#define PKT_Q_VIDEO			1
#define PKT_Q_AUDIO			2
#define PKT_Q_SUBTITLE		3

#define	PkterCmd_None  0x0000
#define	PkterCmd_Exit  0x0001
	int _cmd;

	//put 
	int PutIntoPktQueue(
		FF_PACKET_QUEUE *q,
		AVPacket *pkt);
	int GetPacket(FF_PACKET_QUEUE *, FF_PACKET_LIST **);
	int ClearPktQueue(FF_PACKET_QUEUE *q);
	int DestroyPktQueue(FF_PACKET_QUEUE **);
	int InitPacketQueue(FF_PACKET_QUEUE **, int type);
	void __stdcall ThreadStart();
	static unsigned long __stdcall SThreadStart(void *);
};

#endif