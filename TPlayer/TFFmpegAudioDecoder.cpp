#include "TFFmpegAudioDecoder.h"
#include "TFFmpegUtil.h"

TFFmpegAudioDecoder::TFFmpegAudioDecoder(FFContext *ctx, TFFmpegPacketer *pPkter) :
	_swr(NULL),
	_decFrame(NULL),
	_buffer(NULL),
	_size(0),
	_curPtr(NULL),
	_remainSize(0),
	_outBuffer(NULL),
	_outSize(0),
	_dataSize(0)
{
	_ctx = ctx;
	_pkter = pPkter;
}

TFFmpegAudioDecoder::~TFFmpegAudioDecoder()
{
	if(_buffer)
		free(_buffer);
	if(_outBuffer)
		av_free(_outBuffer);
	if(_swr)
		swr_free(&_swr);
	if(_decFrame)
		avcodec_free_frame(&_decFrame);
}

int TFFmpegAudioDecoder::Init(void)
{
	_channelLayout = _ctx->channelLayout;
	_sampleRate = _ctx->freq;
	_sampleFmt = (int)_ctx->sampleFmt;
	return 0;
}

int TFFmpegAudioDecoder::GetCurFrameInfo(int64_t *pts, int64_t *duration)
{
	int ret = FF_OK;
	if(_decFrame)
	{
		*pts = av_frame_get_best_effort_timestamp(_decFrame);
		if(duration)
			*duration = av_frame_get_pkt_duration(_decFrame);
	}
	else
	{
		ret = FF_ERR_GENERAL;
		*pts = AV_NOPTS_VALUE;
		if(duration)
			*duration = AV_NOPTS_VALUE;
	}
	return ret;
}

int TFFmpegAudioDecoder::Fill(uint8_t *stream, int len)
{
	int ret = FF_OK;
	int remain = len;
	while(remain > 0)
	{
		if(_remainSize > remain)
		{
			memcpy(stream, _curPtr, remain);
			_curPtr += remain;
			_remainSize -= remain;
			remain = 0;
		}
		else
		{
			memcpy(stream, _curPtr, _remainSize);
			remain -= _remainSize;
			stream += _remainSize;
			if(Decode() < 0)
			{
				DebugOutput("Audio decoder go to the end of file.");
				memset(stream, 0, remain);
				ret = FF_EOF;
				break;
			}
			else
			{
				_curPtr = _buffer;
				_remainSize = _dataSize;
			}
		}
	}
	return ret;
}

void TFFmpegAudioDecoder::AllocCtxIfNeeded(const AVFrame *frame)
{
	int64_t decChannelLayout =
		(frame->channel_layout && av_frame_get_channels(frame) == av_get_channel_layout_nb_channels(frame->channel_layout)) ?
		frame->channel_layout : av_get_default_channel_layout(av_frame_get_channels(frame));

	if(_channelLayout != decChannelLayout ||
		_sampleRate != frame->sample_rate ||
		_sampleFmt != frame->format)
	{
		swr_free(&_swr);
		_swr = swr_alloc_set_opts(NULL,
				_ctx->channelLayout, (AVSampleFormat)_ctx->sampleFmt, _ctx->freq,
				decChannelLayout, (AVSampleFormat)frame->format, frame->sample_rate,
				0, NULL);

		if(!_swr || swr_init(_swr) < 0)
		{
			DebugOutput("Cannot init swr context.");
			return;
		}

		_channelLayout = decChannelLayout;
		_sampleRate = frame->sample_rate;
		_sampleFmt = frame->format;
	}
}

int TFFmpegAudioDecoder::Decode()
{
	int ret = FF_OK;
	FFPacketList *pkt = NULL;
	int gotFrame = 0;
	int samples;
	int append = 0;
	uint8_t *from = NULL;
	while(!gotFrame)
	{
		if(_pkter->GetAudioPacket(&pkt) >= 0)
		{
			while(pkt->pkt->size > 0)
			{
				if(!_decFrame)
					_decFrame = avcodec_alloc_frame();
				else
					avcodec_get_frame_defaults(_decFrame);

				int len1 = avcodec_decode_audio4(_ctx->audioStream->codec,
					_decFrame,
					&gotFrame,
					pkt->pkt);

				if(len1 < 0)
				{
					DebugOutput("avcodec_decode_audio4 ret %d", len1);
					break;
				}

				pkt->pkt->data += len1;
				pkt->pkt->size -= len1;

				if(!gotFrame)
					continue;

				AllocCtxIfNeeded(_decFrame);

				if(_swr)
				{
					const uint8_t **in = (const uint8_t **)_decFrame->extended_data;
					int s = _decFrame->nb_samples + 256;
					int minSize  = av_samples_get_buffer_size(NULL,
						av_frame_get_channels(_decFrame),
						s,
						(AVSampleFormat)_ctx->sampleFmt,
						0);
					av_fast_malloc(&_outBuffer, &_outSize, minSize);
					uint8_t **out = &_outBuffer;
					samples = swr_convert(_swr, out, s, in, _decFrame->nb_samples);
					from = _outBuffer;
				}
				else
				{
					samples = _decFrame->nb_samples;
					from = _decFrame->data[0];
				}
				CopyData(from, samples, append);
				append = 1;
			}
			_pkter->FreeSinglePktList(&pkt);
		}
		else
		{
			ret = -1;
			break;
		}
	}
	return ret;
}

int TFFmpegAudioDecoder::CopyData(uint8_t *from, int samples, int append)
{
	int ret = FF_OK;

	int dataSize = av_samples_get_buffer_size(
					NULL,
					av_frame_get_channels(_decFrame),
					samples,
					(AVSampleFormat)_ctx->sampleFmt, 0);
	CheckBuffer(dataSize, append);
	if(append)
	{
		memcpy(_buffer + _dataSize, from, dataSize);
		_dataSize += dataSize;
	}
	else
	{
		memcpy(_buffer, from, dataSize);
		_dataSize = dataSize;
	}

	return ret;
}

void TFFmpegAudioDecoder::CheckBuffer(int newSize, int append)
{
	if(!_buffer)
	{
		_size = newSize;
		_buffer = (uint8_t *)malloc(sizeof(uint8_t) * _size);
		return;
	}
	if(!append && newSize > _size)
	{
		_size = newSize;
		_buffer = (uint8_t *)realloc(_buffer, sizeof(uint8_t) * _size);
		return;
	}
	if(append && (newSize + _dataSize) > _size)
	{
		_size = newSize + _dataSize;
		_buffer = (uint8_t *)realloc(_buffer, sizeof(uint8_t) * _size);
		return;
	}
}