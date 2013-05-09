#ifndef _T_FFMPEG_PLAYER_H_
#define _T_FFMPEG_PLAYER_H_

#include "TFFmpegDef.h"
#include "TFFmpegDecoder.h"
#include "TFFmpegPacketer.h"

class TFFmpegPlayer
{
public:
	TFFmpegPlayer();
	virtual ~TFFmpegPlayer();
	int SetCB(NewFrameCB, FinishedCB);
	int Init(const wchar_t *fileName);
	int Start();
	int Run();
	int Pause();
	int Stop();
	int Step();
	int Seek(double);
	int GetVideoInfo(FFSettings *);
	int GetCurFrame();
private:
	FFContext *_pCtx;
	TFFmpegDecoder *_pDecoder;
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
	int InitCtx(const wchar_t *fileName);
	int PopOneFrame(FFFrame *);
	int GetOneFrame(FFFrame *);
	void OnNewFrame(FFFrame *);
	void OnFinished(void);
	void FreeCtx(void);
	void Uninit(void);
	inline void CmdAndSignal(int);
};
#endif