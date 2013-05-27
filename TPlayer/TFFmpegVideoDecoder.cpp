#include "TFFmpegVideoDecoder.h"
#include "TFFmpegUtil.h"

TFFmpegVideoDecoder::TFFmpegVideoDecoder(FFContext *p, TFFmpegPacketer *pPkter) :
	_swsCtx(NULL),
	_decFrame(NULL)
{
	_ctx = p;
	_pkter = pPkter;
}
TFFmpegVideoDecoder::~TFFmpegVideoDecoder()
{
	if(_swsCtx)
	{
		sws_freeContext(_swsCtx);
		_swsCtx = NULL;
	}
	if(_decFrame)
		avcodec_free_frame(&_decFrame);
}

int TFFmpegVideoDecoder::Init()
{
	AllocSwrContextIfNeeded();
	return 0;
}

void TFFmpegVideoDecoder::AllocSwrContextIfNeeded()
{
	if(_ctx->videoStream->codec->width != _ctx->width ||
		_ctx->videoStream->codec->height != _ctx->height ||
		_ctx->videoStream->codec->pix_fmt != _ctx->pixFmt)
	{
		_swsCtx = sws_getCachedContext(
			_swsCtx,
			_ctx->videoStream->codec->width,
			_ctx->videoStream->codec->height,
			_ctx->videoStream->codec->pix_fmt,
			_ctx->width,
			_ctx->height,
			_ctx->pixFmt,
			SWS_FAST_BILINEAR, NULL, NULL, NULL);
	}
}

int TFFmpegVideoDecoder::SetResolution(int width, int height)
{
	_ctx->width = width;
	_ctx->height = height;

	if(_ctx->videoStream->codec->width != _ctx->width ||
		_ctx->videoStream->codec->height != _ctx->height ||
		_ctx->videoStream->codec->pix_fmt != _ctx->pixFmt)
	{
		_swsCtx = sws_getCachedContext(
			_swsCtx,
			_ctx->videoStream->codec->width,
			_ctx->videoStream->codec->height,
			_ctx->videoStream->codec->pix_fmt,
			_ctx->width,
			_ctx->height,
			_ctx->pixFmt,
			SWS_FAST_BILINEAR, NULL, NULL, NULL);
	}
	else if(!_swsCtx)
	{
		sws_freeContext(_swsCtx);
		_swsCtx = NULL;
	}
	return 0;
}

int TFFmpegVideoDecoder::Decode(FFVideoFrame *frame)
{
	FFPacketList *pPktList = NULL;
	int gotPic = 0;
	int ret = FF_OK;
	if(!frame)
	{
		ret = FF_ERR_NOPOINTER;
		return ret;
	}
	while(!gotPic)
	{
		if(_pkter->GetVideoPacket(&pPktList) >= 0)
		{
			AVPacket *pkt = (AVPacket *)pPktList->pkt;
			if(_decFrame)
				avcodec_get_frame_defaults(_decFrame);
			else
				_decFrame = avcodec_alloc_frame();

			avcodec_decode_video2(_ctx->videoStream->codec,
				_decFrame, &gotPic, pkt);
			if(gotPic)
			{
				frame->frame = avcodec_alloc_frame();
				frame->size = avpicture_get_size(_ctx->pixFmt,
					_ctx->width,
					_ctx->height);
				frame->buffer = (uint8_t *)av_malloc(frame->size * sizeof(uint8_t));
				avpicture_fill((AVPicture *)frame->frame,
					frame->buffer,
					_ctx->pixFmt,
					_ctx->width,
					_ctx->height);

				if(_swsCtx)
				{
					sws_scale(_swsCtx,
						_decFrame->data,
						_decFrame->linesize,
						0,
						_decFrame->height,
						frame->frame->data,
						frame->frame->linesize);
				}
				else
				{
					for(int i = 0; i < 4; i++)
					{
						memcpy(frame->frame->data[i], _decFrame->data[i], _decFrame->linesize[i]);
					}
				}
				frame->width = _ctx->width;
				frame->height = _ctx->height;
				frame->frame->pts = av_frame_get_best_effort_timestamp(_decFrame);
				frame->frame->pkt_duration = av_frame_get_pkt_duration(_decFrame);
			}
			_pkter->FreeSinglePktList(&pPktList);
		}
		else
		{
			ret = FF_EOF;
			break;
		}
	}
	return ret;
}

int TFFmpegVideoDecoder::Free(FFVideoFrame *frame)
{
	int ret = FF_OK;
	if(frame->frame)
		avcodec_free_frame(&frame->frame);
	if(frame->buffer)
		av_free(frame->buffer);
	return ret;
}
