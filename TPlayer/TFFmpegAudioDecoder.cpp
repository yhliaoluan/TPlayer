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
	AVFrame *decFrame = NULL;
	int gotFrame;
	while(true)
	{
		if(_cmd & AUDIO_DECODER_CMD_EXIT)
		{
			DebugOutput("Audio decoder will exit.");
			break;
		}

		FFPacketList *pkt = NULL;
		if(_pkter->GetAudioPacket(&pkt) >= 0)
		{
			while(pkt->pPkt->size > 0)
			{
				if(!decFrame)
					decFrame = avcodec_alloc_frame();
				else
					avcodec_get_frame_defaults(decFrame);

				int len1 = avcodec_decode_audio4(_ctx->audioStream->codec,
					decFrame,
					&gotFrame,
					pkt->pPkt);

				if(len1 < 0)
				{
					DebugOutput("avcodec_decode_audio4 ret %d", len1);
					break;
				}

				pkt->pPkt->data += len1;
				pkt->pPkt->size -= len1;

			}

			_pkter->FreeSinglePktList(&pkt);
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