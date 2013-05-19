#include <Windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <iostream>
#include "TFFmpeg.h"
#include "SDL.h"

extern "C"
{
#include <libavformat\avformat.h>
#include <libswscale\swscale.h>
}

using std::cout;
using std::cin;
using std::endl;

#define CONTROL_HEIGHT 100
#define SDL_AUDIO_BUFFER_SIZE 1024

typedef struct _st_PlayContext
{
	SDL_Surface *window;
	SDL_Overlay *overlay;
} SDLPlayerContext;

static SDLPlayerContext *_context;

static int _beginTime;

void __stdcall NewFrame(FFFrame *p)
{
	SDL_Rect rect;

	//if(!_context->overlay)
	//	_context->overlay = SDL_CreateYUVOverlay(p->width, p->height, SDL_IYUV_OVERLAY, _context->window);

	//if(_context->overlay->w != p->width ||
	//	_context->overlay->h != p->height)
	//{
	//	SDL_FreeYUVOverlay(_context->overlay);
	//	_context->overlay = SDL_CreateYUVOverlay(p->width, p->height, SDL_IYUV_OVERLAY, _context->window);
	//}

	int elMS = SDL_GetTicks() - _beginTime;
	int showMS = (int)(p->time * 1000);
	if(showMS > elMS)
	{
		SDL_Delay(showMS - elMS);
	}

	int err = SDL_LockYUVOverlay(_context->overlay);

	memcpy(_context->overlay->pixels[0], p->data[0], p->linesize[0] * p->height);
	memcpy(_context->overlay->pixels[1], p->data[1], p->linesize[1] * p->height >> 1);
	memcpy(_context->overlay->pixels[2], p->data[2], p->linesize[2] * p->height >> 1);

	SDL_UnlockYUVOverlay(_context->overlay);
	rect.x = 0;
	rect.y = 0;
	rect.w = p->width;
	rect.h = p->height;
	SDL_DisplayYUVOverlay(_context->overlay, &rect);
}

void __stdcall Finished(void)
{
	cout << "Finished." << endl;
}

void AudioCallback(void *userdata, Uint8 *stream, int len)
{
}

static int InitSDL(FFSettings *setting)
{
	int ret = 0;
	ret = SDL_Init(SDL_INIT_EVERYTHING);

	if(ret >= 0)
	{
		_context = (SDLPlayerContext *)malloc(sizeof(SDLPlayerContext));
		memset(_context, 0, sizeof(SDLPlayerContext));
		_context->window = SDL_SetVideoMode(setting->width, setting->height + CONTROL_HEIGHT, 0, SDL_SWSURFACE | SDL_RESIZABLE);

		_context->overlay = SDL_CreateYUVOverlay(setting->width, setting->height, SDL_IYUV_OVERLAY, _context->window);
		SDL_WM_SetCaption("SDL Player", NULL);
	}

	if(ret >= 0)
	{
		SDL_AudioSpec wantedSpec;
		wantedSpec.freq = setting->audioSampleRate;
		wantedSpec.format = AUDIO_S16SYS;
		wantedSpec.channels = setting->audioChannels;
		wantedSpec.silence = 0;
		wantedSpec.samples = SDL_AUDIO_BUFFER_SIZE;
		wantedSpec.callback = AudioCallback;

		SDL_AudioSpec spec;
		SDL_OpenAudio(&wantedSpec, &spec);
	}

	return 0;
}

static void LoopEvents(FFSettings *pSettings, void *handle)
{
	BOOL running = TRUE;

	SDL_Event event;
	int w, h;
	double r;
	double srcR = pSettings->width / (double)pSettings->height;
	while(running)
	{
		SDL_Delay(50);
		while(SDL_PollEvent(&event))
		{
			switch(event.type)
			{
			case SDL_QUIT:
				running = FALSE;
				break;
			case SDL_VIDEORESIZE:
				//w = event.resize.w;
				//h = event.resize.h;
				//r = w / (double)h;
				//if(r > srcR)
				//	w = (int)(h * srcR + 0.5);
				//else if(r < srcR)
				//	h = (int)(w / srcR + 0.5);
				//if(w % 2 != 0)
				//	w -= 1;
				//if(h % 2 != 0)
				//	h -= 1;
				//cout << "Resize w:" << w << " h:" << h << endl;
				//FF_SetResolution(handle, w, h);
				break;
			default:
				break;
			}
		}
	}
}

int player(int argc, wchar_t *argv[])
{
	void *handle;
	FFSettings settings;
	char msg[MAX_PATH] = {0};
	int err = FF_Init();
	if(err >= 0)
	{
		FFInitSetting initSetting = {0};
		wcscpy_s(initSetting.fileName, argv[1]);
		initSetting.dstFramePixFmt = FF_FRAME_PIXFORMAT_YUV420;
		err = FF_InitFile(&initSetting, &settings, &handle);
	}

	if(err >= 0)
	{
		err = FF_SetCallback(handle, NewFrame, Finished);
	}

	if(err >= 0)
	{
		cout << "duration: " << settings.duration * settings.timebaseNum / (double)settings.timebaseDen << endl;
		cout << settings.width << "x" << settings.height << endl;
		cout << "fps: " << settings.fpsNum / (double)settings.fpsDen << endl;
		cout << settings.codecName << endl;
	}

	if(err >= 0)
	{
		InitSDL(&settings);
	}

	_beginTime = SDL_GetTicks();
	FF_Run(handle);

	LoopEvents(&settings, handle);

	if(err >= 0)
	{
		err = FF_CloseHandle(handle);

		if(_context)
		{
			if(_context->window)
				SDL_FreeSurface(_context->window);
			if(_context->overlay)
				SDL_FreeYUVOverlay(_context->overlay);

			free(_context);
			_context = NULL;
		}

		SDL_Quit();
	}

	if(err >= 0)
	{
		cout << "exit." << endl;
	}
	return 0;
}

int test(int argc, wchar_t *argv[])
{
	char szFile[1024] = {0};
	AVFormatContext *fmtCtx = NULL;
	int ret = 0;
	AVPacket *pkt = NULL;
	int videoIndex, audioIndex, subTitleIndex;
	av_register_all();

	WideCharToMultiByte(
		CP_UTF8,
		0,
		argv[1],
		-1,
		szFile,
		1024,
		NULL,
		NULL);

	ret = avformat_open_input(&fmtCtx, szFile, NULL, NULL);
	ret = avformat_find_stream_info(fmtCtx, NULL);

	for(unsigned int i = 0; i < fmtCtx->nb_streams; i++)
	{
		if(fmtCtx->streams[i]->codec->codec_type == AVMEDIA_TYPE_VIDEO)
			videoIndex = i;
		else if(fmtCtx->streams[i]->codec->codec_type == AVMEDIA_TYPE_AUDIO)
			audioIndex = i;
		else if(fmtCtx->streams[i]->codec->codec_type == AVMEDIA_TYPE_SUBTITLE)
			subTitleIndex = i;
		else
			cout << "Unknown codec type " << (int)fmtCtx->streams[i]->codec->codec_type << endl;
	}

	cout << "Video index:" << videoIndex << endl;
	cout << "Audio index:" << audioIndex << endl;
	cout << "Subtitle index:" << subTitleIndex << endl;

	AVPacket *pPkt = (AVPacket *)av_mallocz(sizeof(AVPacket));
	av_init_packet(pPkt);
	char cmd[MAX_PATH] = {0};
	while(cin >> cmd)
	{
		if(strcmp(cmd, "q") == 0)
			break;
		if(av_read_frame(fmtCtx, pPkt) < 0)
			break;
		if(pPkt->stream_index == videoIndex)
			cout << "Packt video";
		else if(pPkt->stream_index == audioIndex)
			cout << "Packt audio";
		else if(pPkt->stream_index == subTitleIndex)
			cout << "Packt subtitle";
		else
			cout << "Packet index:" << pPkt->stream_index;
		cout << " DTS:" << pPkt->dts;
		cout << " Duration:" << pPkt->duration;
		cout << " PTS:" << pPkt->pts;
		cout << " Position:" << pPkt->pos;
		cout << " Size:" << pPkt->size;
		cout << endl;
	}

	av_free_packet(pPkt);
	av_freep(&pPkt);
	avformat_close_input(&fmtCtx);
	return 0;
}

void test_play_sound_callback(void *userdata, uint8_t *stream, int len)
{
	cout << "test_play_sound_callback ";
	cout << "len " << len << endl;
	AVFormatContext *fmtCtx = (AVFormatContext *)userdata;
	AVFrame *frame = avcodec_alloc_frame();
	AVPacket pkt;
	int gotFrame = -1;

	av_init_packet(&pkt);
	while(av_read_frame(fmtCtx, &pkt))
	{
		if(pkt.stream_index == 1)
		{
			break;
		}
	}

	int decSize = avcodec_decode_audio4(fmtCtx->streams[1]->codec,
					frame,
					&gotFrame,
					&pkt);
}

void test_play_sound(wchar_t *file)
{
	char szFile[1024] = {0};
	AVFormatContext *fmtCtx = NULL;
	int ret = 0;
	AVPacket *pkt = NULL;
	int audioIndex;
	av_register_all();

	WideCharToMultiByte(
		CP_UTF8,
		0,
		file,
		-1,
		szFile,
		1024,
		NULL,
		NULL);

	ret = avformat_open_input(&fmtCtx, szFile, NULL, NULL);
	ret = avformat_find_stream_info(fmtCtx, NULL);

	for(unsigned int i = 0; i < fmtCtx->nb_streams; i++)
	{
		if(fmtCtx->streams[i]->codec->codec_type == AVMEDIA_TYPE_AUDIO)
		{
			audioIndex = i;
			break;
		}
	}

	SDL_Init(SDL_INIT_EVERYTHING);

	AVStream *as = fmtCtx->streams[audioIndex];
	AVCodec *codec = avcodec_find_decoder(as->codec->codec_id);
	ret = avcodec_open2(as->codec, codec, NULL);

	SDL_AudioSpec wantedSpec;
	wantedSpec.freq = as->codec->sample_rate;
	wantedSpec.format = AUDIO_S16SYS;
	wantedSpec.channels = as->codec->channels;
	wantedSpec.silence = 0;
	wantedSpec.samples = 1024;
	wantedSpec.callback = test_play_sound_callback;
	wantedSpec.userdata = fmtCtx;

	SDL_AudioSpec spec;
	ret = SDL_OpenAudio(&wantedSpec, &spec);

	if(ret < 0)
	{
		cout << "SDL_OpenAudio err " << ret << endl;
	}

	SDL_PauseAudio(0);

	char msg[MAX_PATH] = {0};
	while(cin >> msg)
	{
		if(strcmp(msg, "q") == 0)
			break;
	}

	cout << "quit" << endl;
	SDL_CloseAudio();
	SDL_Quit();

	avcodec_close(as->codec);
	avformat_close_input(&fmtCtx);
}

int wmain(int argc, wchar_t *argv[])
{
	test_play_sound(argv[1]);

	return 0;
}