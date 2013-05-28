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
	return 0;
}

int TFFmpegVideoDecoder::AllocSwrContextIfNeeded(AVFrame *frame)
{
	if(_curOutputWidth != _ctx->width ||
		_curOutputHeight != _ctx->height ||
		_curOutputPixFmt != _ctx->pixFmt)
	{
		if(frame->width != _ctx->width ||
			frame->height != _ctx->height ||
			(AVPixelFormat)frame->format != _ctx->pixFmt)
		{
			DebugOutput("Alloc sws context. from %d width, %d height, %d pixel format to %d width, %d height, %d pixel format.",
				frame->width, frame->height, frame->format,
				_ctx->width, _ctx->height, (int)_ctx->pixFmt);
			_swsCtx = sws_getCachedContext(
				_swsCtx,
				frame->width,
				frame->height,
				(AVPixelFormat)frame->format,
				_ctx->width,
				_ctx->height,
				_ctx->pixFmt,
				SWS_FAST_BILINEAR, NULL, NULL, NULL);
		}
		else if(_swsCtx)
		{
			sws_freeContext(_swsCtx);
			_swsCtx = NULL;
		}

		_curOutputHeight = _ctx->height;
		_curOutputWidth = _ctx->width;
		_curOutputPixFmt = _ctx->pixFmt;

		return 1;
	}
	return 0;
}

int TFFmpegVideoDecoder::SetResolution(int width, int height)
{
	//TODO: lock and align
	_ctx->width = width;
	_ctx->height = height;
	return 0;
}

int TFFmpegVideoDecoder::Decode(FFVideoFrame *frame)
{
	FFPacketList *pktList = NULL;
	int gotPic = 0;
	int ret = FF_OK;
	if(!frame)
	{
		ret = FF_ERR_NOPOINTER;
		return ret;
	}
	while(!gotPic)
	{
		if(_pkter->GetVideoPacket(&pktList) >= 0)
		{
			AVPacket *pkt = (AVPacket *)pktList->pkt;
			if(_decFrame)
				avcodec_get_frame_defaults(_decFrame);
			else
				_decFrame = avcodec_alloc_frame();

			avcodec_decode_video2(_ctx->videoStream->codec,
				_decFrame, &gotPic, pkt);
			if(gotPic)
			{
				AllocSwrContextIfNeeded(_decFrame);
				if(_swsCtx)
				{
					frame->frame = avcodec_alloc_frame();
					int numBytes = avpicture_get_size(_ctx->pixFmt,
						_ctx->width,
						_ctx->height);
					frame->buffer = (uint8_t *)av_malloc(numBytes * sizeof(uint8_t));
					avpicture_fill((AVPicture *)frame->frame,
						frame->buffer,
						_ctx->pixFmt,
						_ctx->width,
						_ctx->height);

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
					frame->frame = _decFrame;
					frame->buffer = NULL;
				}
				frame->width = _decFrame->width;
				frame->height = _decFrame->height;
				frame->frame->pts = av_frame_get_best_effort_timestamp(_decFrame);
				frame->frame->pkt_duration = av_frame_get_pkt_duration(_decFrame);
				if(frame->frame == _decFrame)
					_decFrame = NULL;
			}
			_pkter->FreeSinglePktList(&pktList);
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
