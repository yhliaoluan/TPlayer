#ifndef _T_FFMPEG_AUDIO_DECODER_H_
#define _T_FFMPEG_AUDIO_DECODER_H_

#include "TFFmpegDef.h"
#include "TFFmpegPacketer.h"

extern "C"
{
#include "libswresample\swresample.h"
}

class TFFmpegAudioDecoder
{
public:
	TFFmpegAudioDecoder(FFContext *, TFFmpegPacketer *);
	~TFFmpegAudioDecoder();

	int Init(void);

	//fill stream with length len
	int Fill(uint8_t *stream, int len, int64_t *pts);
private:
	FFContext *_ctx;
	TFFmpegPacketer *_pkter;
	SwrContext *_swr;
	int64_t _channelLayout;
	int _sampleRate;
	int _sampleFmt;

	uint8_t *_outBuffer;
	size_t _outSize;

	struct TFFAudioFrameList {
		uint8_t *buffer;
		int size;
		uint8_t *curPtr;
		int remainSize;
		int64_t pts;
		struct TFFAudioFrameList *next;
	} *_rawFrames;

	AVFrame *_decFrame;

	int Decode();
	void CheckBuffer(int newSize);
	void AllocCtxIfNeeded(const AVFrame *);
	int CopyData(AVFrame *frame);
};

#endif