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
private:
	TFF_Thread _thread;
	FFContext *_ctx;
	TFFmpegPacketer *_pkter;

#define AUDIO_DECODER_CMD_NONE 0x0000
#define AUDIO_DECODER_CMD_EXIT  0x0001
	int _cmd;

	void __stdcall ThreadStart();
	static unsigned long __stdcall SThreadStart(void *);
};

#endif