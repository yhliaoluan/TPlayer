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

	int Decode(FFVideoFrame *);
	int Free(FFVideoFrame *);
private:
	FFContext *_ctx;
	TFFmpegPacketer *_pkter;
	SwsContext *_swsCtx;

	AVFrame *_decFrame;

	void AllocSwrContextIfNeeded();
};
#endif