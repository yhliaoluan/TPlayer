#ifndef _T_FFMPEG_PLAYER_H_
#define _T_FFMPEG_PLAYER_H_

#include "TFFmpegDef.h"
#include "TFFmpegVideoDecoder.h"
#include "TFFmpegPacketer.h"
#include "TFFmpegAudioDecoder.h"
#include "TFFmpegClock.h"

class TFFmpegPlayer
{
public:
	TFFmpegPlayer();
	virtual ~TFFmpegPlayer();
	int SetCB(NewFrameCB, FinishedCB);
	int Init(const FFInitSetting *pSetting);
	int Start();
	int Run();
	int Pause();
	int Stop();
	int Step();
	int Seek(double);
	int GetVideoInfo(FFSettings *);
	int GetCurFrame();
	int SetResolution(int w, int h);
	int FillAudioStream(uint8_t *stream, int len);
private:
	FFContext *_ctx;
	TFFmpegVideoDecoder *_videoDecoder;
	TFFmpegAudioDecoder *_audioDecoder;
	TFFmpegPacketer *_pkter;
	TFF_Thread _thread;
	NewFrameCB _fNewFrameCB;
	FinishedCB _fFinishedCB;
	TFF_Mutex _cmdMutex;
	TFF_Mutex _flushMutex;
	TFF_Cond _cmdCond;
	double _seekTime;
	TFFmpegClock _clock;

	int _audioWait;
	int _useExternalClock;

	enum
	{
		Player_Cmd_None = 0x0000,
		Player_Cmd_Run  = 0x0001,
		Player_Cmd_Exit = 0x0002,
		Player_Cmd_Seek = 0x0004,
		Player_Cmd_Step = 0x0008,
		Player_Cmd_Stop = 0x0010,
		Player_Cmd_Get  = 0x0020,
		Player_Cmd_Pause = 0x0040
	};

	int _cmd;

	enum { TFF_PLAYTYPE_VIDEO = 0x0001, TFF_PLAYTYPE_AUDIO = 0x0002 };
	int _finishedType;

	static unsigned long __stdcall SThreadStart(void *);
	void ThreadStart(void);
	int InitCtx(const FFInitSetting *pSetting);
	inline void OnNewFrame(IN FFFrame *);
	void OnFinished(int type);
	void FreeCtx(void);
	void Uninit(void);
	int OpenVideoCodec(void);
	int OpenAudioCodec(void);
	inline int Convert(FFVideoFrame *, FFFrame *);
	inline void SyncVideo(FFFrame *);
	inline void CmdAndSignal(int);
};
#endif