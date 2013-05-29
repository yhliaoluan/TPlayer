#ifndef _T_FFMPEG_VIDEO_DECODER_H_
#define _T_FFMPEG_VIDEO_DECODER_H_

#include "TFFmpegDef.h"
#include "TFFmpegPacketer.h"

class TFFmpegVideoDecoder
{
public:
	TFFmpegVideoDecoder(FFContext *, TFFmpegPacketer *pPkter);
	virtual ~TFFmpegVideoDecoder();
	int Init();
	int SetResolution(int width, int height);
	int SetOutputSetting(FFVideoSetting *setting);
	int Decode(FFVideoFrame *);
	int Free(FFVideoFrame *);
private:
	FFContext *_ctx;
	TFFmpegPacketer *_pkter;
	SwsContext *_swsCtx;
	TFF_Mutex _settingMutex;
	FFVideoSetting _outputSetting;
	int _outputSettingChanged;

	AVFrame *_decFrame;

	int AllocSwrContextIfNeeded(AVFrame *frame);
};
#endif