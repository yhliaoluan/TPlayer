#ifndef _T_FFMPEG_PLAYER_H_
#define _T_FFMPEG_PLAYER_H_

#include "TFFmpegDef.h"
#include "TFFmpegVideoDecoder.h"
#include "TFFmpegPacketer.h"
#include "TFFmpegAudioDecoder.h"

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
	int PopAudioFrame(FFFrame *);
	int FreeAudioFrame(FFFrame *);
private:
	FFContext *_pCtx;
	TFFmpegVideoDecoder *_pDecoder;
	TFFmpegAudioDecoder *_audioDecoder;
	TFFmpegPacketer *_pPkter;
	TFF_Thread _hThread;
	NewFrameCB _fNewFrameCB;
	FinishedCB _fFinishedCB;
	TFF_Mutex _cmdMutex;
	TFF_Cond _cmdCond;

#define Player_Cmd_None 0x0000
#define Player_Cmd_Run  0x0001
#define Player_Cmd_Exit 0x0002
#define Player_Cmd_Seek 0x0004
#define Player_Cmd_Step 0x0008
#define Player_Cmd_Stop 0x0010
#define Player_Cmd_Get  0x0020
#define Player_Cmd_Pause 0x0040

	int _cmd;
	int64_t _seekPos;

	static unsigned long __stdcall SThreadStart(void *);
	void ThreadStart(void);
	int InitCtx(const FFInitSetting *pSetting);
	int PopOneFrame(OUT FFFrame *, FFVideoFrame **);
	void OnNewFrame(IN FFFrame *);
	void OnFinished(void);
	void FreeCtx(void);
	void Uninit(void);
	int OpenVideoCodec(void);
	int OpenAudioCodec(void);
	inline void CmdAndSignal(int);
};
#endif