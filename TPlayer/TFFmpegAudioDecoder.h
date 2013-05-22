#ifndef _T_FFMPEG_AUDIO_DECODER_H_
#define _T_FFMPEG_AUDIO_DECODER_H_

#include "TFFmpegDef.h"
#include "TFFmpegPacketer.h"

class TFFmpegAudioDecoder
{
public:
	TFFmpegAudioDecoder(FFContext *, TFFmpegPacketer *);
	~TFFmpegAudioDecoder();

	int Start();
	int Init();
	int FreeSingleFrame(FFAudioFrame **);
	int Pop(FFAudioFrame **);
private:
	TFF_Thread _thread;
	TFF_Mutex _readMutex;
	TFF_Cond _readCond;
	FFContext *_ctx;
	TFFmpegPacketer *_pkter;
	FFFrameQueue *_queue;
	int _finished;

#define AUDIO_DECODER_CMD_NONE 0x0000
#define AUDIO_DECODER_CMD_EXIT  0x0001
	int _cmd;

	void __stdcall ThreadStart();
	static unsigned long __stdcall SThreadStart(void *);
	int PutIntoQueue(uint8_t *buffer, int size);
	int ClearFrameQueue();
	int DestroyFrameQueue();
};

#endif