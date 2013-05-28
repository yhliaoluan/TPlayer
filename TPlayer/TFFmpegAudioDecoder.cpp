#include "TFFmpegAudioDecoder.h"
#include "TFFmpegUtil.h"

TFFmpegAudioDecoder::TFFmpegAudioDecoder(FFContext *ctx, TFFmpegPacketer *pPkter) :
	_swr(NULL),
	_outBuffer(NULL),
	_outSize(0),
	_rawFrames(NULL),
	_decFrame(NULL)
{
	_ctx = ctx;
	_pkter = pPkter;
}

TFFmpegAudioDecoder::~TFFmpegAudioDecoder()
{
	TFFAudioFrameList *cur = _rawFrames;
	TFFAudioFrameList *next = NULL;
	if(_outBuffer)
		av_free(_outBuffer);
	if(_swr)
		swr_free(&_swr);
	if(_decFrame)
		avcodec_free_frame(&_decFrame);
	while(cur)
	{
		next = cur->next;
		av_free(cur->buffer);
		av_free(cur);
		cur = next;
	}
}

int TFFmpegAudioDecoder::Init(void)
{
	_channelLayout = _ctx->channelLayout;
	_sampleRate = _ctx->freq;
	_sampleFmt = (int)_ctx->sampleFmt;
	return 0;
}

int TFFmpegAudioDecoder::Fill(uint8_t *stream, int len, int64_t *pts)
{
	int ret = FF_OK;
	TFFAudioFrameList *next;
	*pts = AV_NOPTS_VALUE;
	while(len > 0)
	{
		if(!_rawFrames)
		{
			if(Decode() < 0)
			{
				DebugOutput("Audio decoder go to the end of file.");
				ret = FF_EOF;
				memset(stream, 0, len);
				break;
			}
		}
		if(*pts == AV_NOPTS_VALUE && _rawFrames->pts != AV_NOPTS_VALUE)
			*pts = (int64_t)(_rawFrames->duration * (1 - (double)_rawFrames->remainSize / _rawFrames->size) + _rawFrames->pts);
		if(_rawFrames->remainSize > len)
		{
			memcpy(stream, _rawFrames->curPtr, len);
			_rawFrames->curPtr += len;
			_rawFrames->remainSize -= len;
			len = 0;
		}
		else
		{
			memcpy(stream, _rawFrames->curPtr, _rawFrames->remainSize);
			len -= _rawFrames->remainSize;
			stream += _rawFrames->remainSize;
			next = _rawFrames->next;
			av_free(_rawFrames->buffer);
			av_free(_rawFrames);
			_rawFrames = next;
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
		DebugOutput("Alloc swr context. from %I64d channel layout, %d format, %d freq to %I64d channel layout, %d format, %d freq",
			decChannelLayout, frame->format, frame->sample_rate,
			_ctx->channelLayout, (int)_ctx->sampleFmt, _ctx->freq);
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
				CopyData(_decFrame, pkt->pkt);
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

int TFFmpegAudioDecoder::CopyData(AVFrame *frame, AVPacket *pkt)
{
	int ret = FF_OK;
	int samples, dataSize, s, minSize;
	uint8_t *from;
	const uint8_t **in;
	uint8_t **out;
	AVRational tb = { 1, frame->sample_rate };

	if(_swr)
	{
		in = (const uint8_t **)frame->extended_data;
		s = frame->nb_samples + 256;
		minSize  = av_samples_get_buffer_size(NULL,
			av_frame_get_channels(frame),
			s,
			(AVSampleFormat)_ctx->sampleFmt,
			0);
		av_fast_malloc(&_outBuffer, &_outSize, minSize);
		out = &_outBuffer;
		samples = swr_convert(_swr, out, s, in, frame->nb_samples);
		from = _outBuffer;
		dataSize = samples * av_frame_get_channels(frame) * av_get_bytes_per_sample(_ctx->sampleFmt);
	}
	else
	{
		dataSize = av_samples_get_buffer_size(
					NULL,
					av_frame_get_channels(frame),
					frame->nb_samples,
					_ctx->sampleFmt, 1);
		from = frame->data[0];
	}

	TFFAudioFrameList *n = (TFFAudioFrameList *)av_malloc(sizeof(TFFAudioFrameList));
	if(!_rawFrames)
		_rawFrames = n;
	else
	{
		TFFAudioFrameList *cur = _rawFrames;
		while(cur->next)
			cur = cur->next;
		cur->next = n;
	}
	n->buffer = (uint8_t *)av_malloc(sizeof(uint8_t) * dataSize);
	memcpy(n->buffer, from, dataSize);
	n->size = dataSize;
	n->curPtr = n->buffer;
	n->remainSize = n->size;
	n->next = NULL;
	if(frame->pts != AV_NOPTS_VALUE)
		n->pts = (int64_t)(frame->pts * av_q2d(_ctx->audioStream->codec->time_base) * 1000);
	else if(pkt->pts != AV_NOPTS_VALUE)
		n->pts = (int64_t)(pkt->pts * av_q2d(_ctx->audioStream->time_base) * 1000);
	else n->pts = AV_NOPTS_VALUE;

	n->duration = (int64_t)((double)frame->nb_samples / frame->sample_rate * 1000);

	return ret;
}
