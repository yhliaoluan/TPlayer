#include "TFFmpegAudioDecoder.h"
#include "TFFmpegUtil.h"

extern "C"
{
#include "libswresample\swresample.h"
}

//1MB
#define FF_MAX_CACHED_FRAME_SIZE 0x00100000

TFFmpegAudioDecoder::TFFmpegAudioDecoder(FFContext *ctx, TFFmpegPacketer *pPkter) :
	_thread(NULL),
	_cmd(AUDIO_DECODER_CMD_NONE),
	_queue(NULL),
	_finished(0)
{
	_ctx = ctx;
	_pkter = pPkter;
}

TFFmpegAudioDecoder::~TFFmpegAudioDecoder()
{
	if(_thread)
	{
		_cmd |= AUDIO_DECODER_CMD_EXIT;
		TFF_CondBroadcast(_readCond);
		TFF_WaitThread(_thread, 1000);
		TFF_CloseThread(_thread);
		_thread = NULL;
	}
	DestroyFrameQueue();
	TFF_CloseMutex(_readMutex);
	TFF_DestroyCond(&_readCond);
}

int TFFmpegAudioDecoder::Init()
{
	_queue = (FFFrameQueue *)av_malloc(sizeof(FFFrameQueue));
	_queue->mutex = TFF_CreateMutex();
	_queue->cond = TFF_CreateCond();
	_queue->count = 0;
	_queue->size = 0;
	_queue->firstA = _queue->lastA = NULL;

	_readMutex = TFF_CreateMutex();
	_readCond = TFF_CreateCond();
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
	AVFrame *frame = NULL;
	FFPacketList *pkt;
	SwrContext *swr = NULL;
	uint8_t *buff = NULL;
	uint8_t *outbuff = NULL;
	size_t outsize = 0;
	int gotFrame, dataSize;
	int64_t srcChannelLayout = _ctx->channelLayout;
	int srcSampleRate = _ctx->freq;
	int srcSampleFmt = (int)_ctx->sampleFmt;

	DebugOutput("TFFmpegAudioDecoder::Thread begin.");
	while(1)
	{
		TFF_GetMutex(_readMutex, TFF_INFINITE);
		while(_queue->size >= FF_MAX_CACHED_FRAME_SIZE &&
			!(_cmd & AUDIO_DECODER_CMD_EXIT))
			TFF_WaitCond(_readCond, _readMutex);

		if(_cmd & AUDIO_DECODER_CMD_EXIT)
			break;

		if(_pkter->GetAudioPacket(&pkt) >= 0)
		{
			while(pkt->pPkt->size > 0)
			{
				if(!frame)
					frame = avcodec_alloc_frame();
				else
					avcodec_get_frame_defaults(frame);

				int len1 = avcodec_decode_audio4(_ctx->audioStream->codec,
					frame,
					&gotFrame,
					pkt->pPkt);

				if(len1 < 0)
				{
					DebugOutput("avcodec_decode_audio4 ret %d", len1);
					break;
				}

				pkt->pPkt->data += len1;
				pkt->pPkt->size -= len1;

				if(!gotFrame)
					continue;

				int64_t decChannelLayout =
					(frame->channel_layout && av_frame_get_channels(frame) == av_get_channel_layout_nb_channels(frame->channel_layout)) ?
					frame->channel_layout : av_get_default_channel_layout(av_frame_get_channels(frame));

				if(srcChannelLayout != decChannelLayout ||
					srcSampleRate != frame->sample_rate ||
					srcSampleFmt != frame->format)
				{
					swr_free(&swr);
					swr = swr_alloc_set_opts(NULL,
							_ctx->channelLayout, (AVSampleFormat)_ctx->sampleFmt, _ctx->freq,
                            decChannelLayout, (AVSampleFormat)frame->format, frame->sample_rate,
                            0, NULL);

					if(!swr || swr_init(swr) < 0)
					{
						DebugOutput("Cannot init swr context.");
						break;
					}

					srcChannelLayout = decChannelLayout;
					srcSampleRate = frame->sample_rate;
					srcSampleFmt = frame->format;
				}

				if(swr)
				{
					const uint8_t **in = (const uint8_t **)frame->extended_data;
					int out_count = frame->nb_samples + 256;
					int out_size  = av_samples_get_buffer_size(NULL,
						av_frame_get_channels(frame),
						out_count,
						(AVSampleFormat)_ctx->sampleFmt,
						0);

					av_fast_malloc(&outbuff, &outsize, out_size);

					uint8_t **out = &outbuff;
					int len2 = swr_convert(swr, out, out_count, in, frame->nb_samples);

					int resampled_data_size = av_samples_get_buffer_size(
						NULL,
						av_frame_get_channels(frame),
						len2,
						(AVSampleFormat)_ctx->sampleFmt,
						0);
					buff = (uint8_t *)av_malloc(sizeof(uint8_t) * resampled_data_size);
					memcpy(buff, outbuff, resampled_data_size);
					PutIntoQueue(buff, resampled_data_size);
				}
				else
				{
					dataSize = av_samples_get_buffer_size(NULL,
						av_frame_get_channels(frame),
						frame->nb_samples,
						(AVSampleFormat)frame->format, 1);
					buff = (uint8_t *)av_malloc(sizeof(uint8_t) * dataSize);
					memcpy(buff, frame->data[0], dataSize);
					PutIntoQueue(buff, dataSize);
				}
			}
			_pkter->FreeSinglePktList(&pkt);
		}
		else
		{
			_finished = TRUE;
			TFF_CondBroadcast(_queue->cond);
			TFF_WaitCond(_readCond, _readMutex);
		}
		TFF_ReleaseMutex(_readMutex);
	}
	if(swr)
		swr_free(&swr);
	if(outbuff)
		av_freep(&outbuff);
	DebugOutput("Audio decoder exit.");
}

int TFFmpegAudioDecoder::Pop(FFAudioFrame **ppf)
{
	int ret = 0;
	TFF_GetMutex(_queue->mutex, TFF_INFINITE);
	while(_queue->count == 0 &&
		!_finished)
		TFF_WaitCond(_queue->cond, _queue->mutex);

	if(_queue->count == 0 && _finished)
	{
		*ppf = NULL;
		ret = -1;
	}
	else
	{
		*ppf = _queue->firstA;
		_queue->size -= _queue->firstA->size;
		_queue->firstA = _queue->firstA->next;
		_queue->count--;
		if(_queue->count <= 1)
			TFF_CondBroadcast(_readCond);
	}
	TFF_ReleaseMutex(_queue->mutex);
	return ret;
}

int TFFmpegAudioDecoder::FreeSingleFrame(FFAudioFrame **ppf)
{
	if(ppf && *ppf)
	{
		if((*ppf)->buffer)
			av_free((*ppf)->buffer);
		av_freep(ppf);
	}
	return 0;
}

int TFFmpegAudioDecoder::PutIntoQueue(uint8_t *buffer, int size)
{
	FFAudioFrame *frame = (FFAudioFrame *)av_malloc(sizeof(FFAudioFrame));
	frame->buffer = buffer;
	frame->size = size;
	frame->next = NULL;

	TFF_GetMutex(_queue->mutex, TFF_INFINITE);
	if(_queue->count == 0)
		_queue->firstA = _queue->lastA = frame;
	else
	{
		_queue->lastA->next = frame;
		_queue->lastA = frame;
	}

	_queue->count++;
	_queue->size += frame->size;
	if(_queue->count == 1)
		TFF_CondBroadcast(_queue->cond);
	TFF_ReleaseMutex(_queue->mutex);
	return 0;
}

unsigned long __stdcall TFFmpegAudioDecoder::SThreadStart(void *p)
{
	TFFmpegAudioDecoder *t = (TFFmpegAudioDecoder *)p;
	t->ThreadStart();
	return 0;
}

int TFFmpegAudioDecoder::DestroyFrameQueue()
{
	ClearFrameQueue();
	TFF_CloseMutex(_queue->mutex);
	TFF_DestroyCond(&_queue->cond);
	av_freep(&_queue);
	return FF_OK;
}

int TFFmpegAudioDecoder::ClearFrameQueue()
{
	FFAudioFrame *cur = _queue->firstA;
	FFAudioFrame *next = NULL;
	while(cur != NULL)
	{
		next = cur->next;
		FreeSingleFrame(&cur);
		cur = next;
	}
	_queue->firstA = _queue->lastA = NULL;
	_queue->count = 0;
	_queue->size = 0;
	return FF_OK;
}