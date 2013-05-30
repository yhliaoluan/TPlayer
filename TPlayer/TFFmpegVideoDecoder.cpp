#include "TFFmpegVideoDecoder.h"
#include "TFFmpegUtil.h"

TFFmpegVideoDecoder::TFFmpegVideoDecoder(FF_CONTEXT *p, TFFmpegPacketer *pPkter) :
	_swsCtx(NULL),
	_decFrame(NULL),
	_outputSettingChanged(0),
	_settingMutex(NULL)
{
	_ctx = p;
	_pkter = pPkter;
}
TFFmpegVideoDecoder::~TFFmpegVideoDecoder()
{
	if (_swsCtx)
	{
		sws_freeContext(_swsCtx);
		_swsCtx = NULL;
	}
	if (_decFrame)
		avcodec_free_frame(&_decFrame);
	if (_settingMutex)
	{
		TFF_CloseMutex(_settingMutex);
		_settingMutex = NULL;
	}
}

int TFFmpegVideoDecoder::Init()
{
	_settingMutex = TFF_CreateMutex();
	return 0;
}

int TFFmpegVideoDecoder::AllocSwrContextIfNeeded(AVFrame *frame)
{
	if (_outputSettingChanged)
	{
		TFF_GetMutex(_settingMutex, TFF_INFINITE);
		TFFLog(TFF_LOG_LEVEL_INFO, "Alloc sws context. from %d width, %d height, %d pixel format to %d width, %d height, %d pixel format.",
			frame->width, frame->height, frame->format,
			_outputSetting.width, _outputSetting.height, (int)_outputSetting.pixFmt);
		_swsCtx = sws_getCachedContext(
			_swsCtx,
			frame->width,
			frame->height,
			(AVPixelFormat)frame->format,
			_outputSetting.width,
			_outputSetting.height,
			_outputSetting.pixFmt,
			SWS_FAST_BILINEAR, NULL, NULL, NULL);

		_outputSettingChanged = 0;
		TFF_ReleaseMutex(_settingMutex);
		return 1;
	}
	return 0;
}

int TFFmpegVideoDecoder::SetOutputSetting(FF_VIDEO_SETTING *setting)
{
	TFF_GetMutex(_settingMutex, TFF_INFINITE);
	if (setting->width > 0)
		_outputSetting.width = setting->width;
	if (setting->height > 0)
		_outputSetting.height = setting->height;
	if (setting->pixFmt >= 0)
		_outputSetting.pixFmt = setting->pixFmt;
	_outputSettingChanged = 1;
	TFF_ReleaseMutex(_settingMutex);
	return FF_OK;
}

int TFFmpegVideoDecoder::SetResolution(int width, int height)
{
	TFF_GetMutex(_settingMutex, TFF_INFINITE);
	if (_outputSetting.width != width ||
		_outputSetting.height != height)
	{
		_outputSetting.width = width;
		_outputSetting.height = height;
		_outputSettingChanged = 1;
	}
	TFF_ReleaseMutex(_settingMutex);
	return 0;
}

int TFFmpegVideoDecoder::Decode(FF_VIDEO_FRAME *frame)
{
	FF_PACKET_LIST *pktList = NULL;
	int gotPic = 0;
	int ret = FF_OK;
	int w, h;
	AVPixelFormat fmt;
	if (!frame)
	{
		ret = FF_ERR_NOPOINTER;
		return ret;
	}
	while (!gotPic)
	{
		if (_pkter->GetVideoPacket(&pktList) >= 0)
		{
			AVPacket *pkt = (AVPacket *)pktList->pkt;
			if (_decFrame)
				avcodec_get_frame_defaults(_decFrame);
			else
				_decFrame = avcodec_alloc_frame();

			avcodec_decode_video2(_ctx->videoStream->codec,
				_decFrame, &gotPic, pkt);
			if (gotPic)
			{
				TFF_GetMutex(_settingMutex, TFF_INFINITE);
				w = _outputSetting.width;
				h = _outputSetting.height;
				fmt = _outputSetting.pixFmt;
				TFF_ReleaseMutex(_settingMutex);
				AllocSwrContextIfNeeded(_decFrame);
				frame->frame = avcodec_alloc_frame();
				int numBytes = avpicture_get_size(fmt, w, h);
				frame->buffer = (uint8_t *)av_malloc(numBytes * sizeof(uint8_t));
				avpicture_fill((AVPicture *)frame->frame, frame->buffer, fmt, w, h);

				sws_scale(_swsCtx,
					_decFrame->data,
					_decFrame->linesize,
					0,
					_decFrame->height,
					frame->frame->data,
					frame->frame->linesize);
				frame->width = w;
				frame->height = h;
				frame->frame->pts = av_frame_get_best_effort_timestamp(_decFrame);
				frame->frame->pkt_duration = av_frame_get_pkt_duration(_decFrame);
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

int TFFmpegVideoDecoder::Free(FF_VIDEO_FRAME *frame)
{
	int ret = FF_OK;
	if (frame->frame)
		avcodec_free_frame(&frame->frame);
	if (frame->buffer)
		av_free(frame->buffer);
	return ret;
}
