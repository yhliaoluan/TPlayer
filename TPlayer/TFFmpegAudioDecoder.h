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

	int Init();
	int SetOutputSetting(FFAudioSetting *setting);
	int GetOutputSetting(FFAudioSetting *setting);

	//fill stream with length len and out put pts in millisecond
	int Fill(uint8_t *stream, int len, int64_t *pts);
private:
	FFContext *_ctx;
	TFFmpegPacketer *_pkter;
	SwrContext *_swr;

	FFAudioSetting _outputSetting;
	int _outputSettingChanged;

	uint8_t *_outBuffer;
	size_t _outSize;

	struct TFFAudioFrameList {
		uint8_t *buffer;
		int size;
		uint8_t *curPtr;
		int remainSize;
		int64_t pts; //in ms
		int64_t duration; //in ms
		struct TFFAudioFrameList *next;
	} *_rawFrames;

	AVFrame *_decFrame;

	int Decode();
	int AllocCtxIfNeeded(const AVFrame *);
	int CopyData(AVFrame *frame, AVPacket *pkt);
};

#endif