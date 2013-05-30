#ifndef _T_FFMPEG_VIDEO_DECODER_H_
#define _T_FFMPEG_VIDEO_DECODER_H_

#include "TFFmpegDef.h"
#include "TFFmpegPacketer.h"

class TFFmpegVideoDecoder
{
public:
	TFFmpegVideoDecoder(FF_CONTEXT *, TFFmpegPacketer *pPkter);
	virtual ~TFFmpegVideoDecoder();
	int Init();
	int SetResolution(int width, int height);
	int SetOutputSetting(FF_VIDEO_SETTING *setting);
	int Decode(FF_VIDEO_FRAME *);
	int Free(FF_VIDEO_FRAME *);
private:
	FF_CONTEXT *_ctx;
	TFFmpegPacketer *_pkter;
	SwsContext *_swsCtx;
	TFF_Mutex _settingMutex;
	FF_VIDEO_SETTING _outputSetting;
	int _outputSettingChanged;

	AVFrame *_decFrame;

	int AllocSwrContextIfNeeded(AVFrame *frame);
};
#endif