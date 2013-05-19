#include "TFFmpegAudioDecoder.h"
#include "TFFmpegUtil.h"

TFFmpegAudioDecoder::TFFmpegAudioDecoder(FFContext *ctx, TFFmpegPacketer *pPkter)
	: _thread(NULL),
	_cmd(AUDIO_DECODER_CMD_NONE)
{
	_ctx = ctx;
	_pkter = pPkter;
}

TFFmpegAudioDecoder::~TFFmpegAudioDecoder()
{
	if(_thread)
	{
		TFF_WaitThread(_thread, 1000);
		TFF_CloseThread(_thread);
		_thread = NULL;
	}
}

int TFFmpegAudioDecoder::Init()
{
	return 0;
}

int TFFmpegAudioDecoder::Start()
{
	if(!_thread)
		_thread = TFF_CreateThread(SThreadStart, this);
	return 0;
}

void __stdcall TFFmpegAudioDecoder::ThreadStart()
{
	DebugOutput("TFFmpegAudioDecoder::Thread begin.");
	while(true)
	{
		if(_cmd & AUDIO_DECODER_CMD_EXIT)
		{
			DebugOutput("Audio decoder will exit.");
			break;
		}
	}
	DebugOutput("Audio decoder exit.");
}

unsigned long __stdcall TFFmpegAudioDecoder::SThreadStart(void *p)
{
	TFFmpegAudioDecoder *t = (TFFmpegAudioDecoder *)p;
	t->ThreadStart();
	return 0;
}