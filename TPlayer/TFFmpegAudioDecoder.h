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
	int Fill(uint8_t *stream, int len);
	int GetCurFrameInfo(int64_t *pts, int64_t *duration);
private:
	FFContext *_ctx;
	TFFmpegPacketer *_pkter;
	SwrContext *_swr;
	int64_t _channelLayout;
	int _sampleRate;
	int _sampleFmt;
	uint8_t *_buffer; // store the whole buff data
	int _size; //buff size
	int _dataSize;
	uint8_t *_curPtr;// current pointer of buff data
	int _remainSize;

	uint8_t *_outBuffer;
	size_t _outSize;

	AVFrame *_decFrame;

	int Decode();
	void CheckBuffer(int newSize, int append);
	void AllocCtxIfNeeded(const AVFrame *);
	int CopyData(uint8_t *, int samples, int append);
};

#endif