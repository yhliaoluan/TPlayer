#include <Windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string>
#include <iostream>
#include "TFFmpeg.h"
#include "SDL.h"
#include "SDL_opengl.h"
#include "log.h"

using std::cout;
using std::cin;
using std::endl;

#define CONTROL_HEIGHT 100
#define SDL_AUDIO_BUFFER_SIZE 1024
#define SDL_VIDEO_BPP 24

typedef struct _st_PlayContext
{
	SDL_Surface *window;
	SDL_Overlay *overlay;
	SDL_Surface *video;
	SDL_Surface *background;
	void *handle; //player handle
} SDLPlayerContext;

void AllocSDLSurface(SDL_Surface **dst, int w, int h, SDL_Surface *src);
void AllocSDLSurface(SDL_Surface **dst, SDL_Surface *src);

static SDLPlayerContext *_context;

static void HandleYUVFrame(FFFrame *p)
{
	SDL_Rect rect;
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

static void HandleRGB24Frame(FFFrame *p)
{
	SDL_Rect dstRect;
	if(_context->video->w != p->width ||
		_context->video->h != p->height)
	{
		AllocSDLSurface(&_context->video, p->width, p->height, _context->window);
		SDL_BlitSurface(_context->background, NULL, _context->window, NULL);
	}

	dstRect.x = (_context->window->w - _context->video->w) >> 1;
	dstRect.y = (_context->window->h - _context->video->h) >> 1;
	dstRect.w = _context->video->w;
	dstRect.h = _context->video->h;
	SDL_LockSurface(_context->video);
	memcpy(_context->video->pixels, p->data[0], p->linesize[0] * p->height);
	SDL_UnlockSurface(_context->video);
	SDL_BlitSurface(_context->video, NULL, _context->window, &dstRect);

	SDL_Flip(_context->window);
}

void __stdcall NewFrame(FFFrame *p)
{
	HandleRGB24Frame(p);
}

void __stdcall Finished(void)
{
	cout << "Finished." << endl;
	SDL_Event e;
	e.type = SDL_QUIT;
	SDL_PushEvent(&e);
}

void AudioCallback(void *userdata, Uint8 *stream, int len)
{
	SDLPlayerContext *context = (SDLPlayerContext *)userdata;
	FF_CopyAudioStream(context->handle, stream, len);
}

static int audio_open(void *opaque,
					  int64_t wanted_channel_layout,
					  int wanted_nb_channels,
					  int wanted_sample_rate,
					  int64_t *obtainedChannelLayout,
					  int *obtainedChannels)
{
    SDL_AudioSpec wanted_spec, spec;
    const int next_nb_channels[] = {0, 0, 1, 6, 2, 6, 4, 6};

    if (!wanted_channel_layout || wanted_nb_channels != av_get_channel_layout_nb_channels(wanted_channel_layout)) {
        wanted_channel_layout = av_get_default_channel_layout(wanted_nb_channels);
        wanted_channel_layout &= ~AV_CH_LAYOUT_STEREO_DOWNMIX;
    }
    wanted_spec.channels = av_get_channel_layout_nb_channels(wanted_channel_layout);
    wanted_spec.freq = wanted_sample_rate;
    if (wanted_spec.freq <= 0 || wanted_spec.channels <= 0) {
        fprintf(stderr, "Invalid sample rate or channel count!\n");
        return -1;
    }
    wanted_spec.format = AUDIO_S16SYS;
    wanted_spec.silence = 0;
    wanted_spec.samples = SDL_AUDIO_BUFFER_SIZE;
    wanted_spec.callback = AudioCallback;
    wanted_spec.userdata = opaque;
    while (SDL_OpenAudio(&wanted_spec, &spec) < 0) {
        fprintf(stderr, "SDL_OpenAudio (%d channels): %s\n", wanted_spec.channels, SDL_GetError());
        wanted_spec.channels = next_nb_channels[FFMIN(7, wanted_spec.channels)];
        if (!wanted_spec.channels) {
            fprintf(stderr, "No more channel combinations to try, audio open failed\n");
            return -1;
        }
        wanted_channel_layout = av_get_default_channel_layout(wanted_spec.channels);
    }
    if (spec.format != AUDIO_S16SYS) {
        fprintf(stderr, "SDL advised audio format %d is not supported!\n", spec.format);
        return -1;
    }
    if (spec.channels != wanted_spec.channels) {
        wanted_channel_layout = av_get_default_channel_layout(spec.channels);
        if (!wanted_channel_layout) {
            fprintf(stderr, "SDL advised channel count %d is not supported!\n", spec.channels);
            return -1;
        }
    }

	if(obtainedChannelLayout)
		*obtainedChannelLayout = wanted_channel_layout;
	if(obtainedChannels)
		*obtainedChannels = spec.channels;

    return spec.size;
}

static void AllocSDLSurface(SDL_Surface **dst, int w, int h, SDL_Surface *src)
{
	if(*dst)
		SDL_FreeSurface(*dst);
	*dst = SDL_CreateRGBSurface(SDL_HWSURFACE,
		w,
		h,
		src->format->BitsPerPixel,
		src->format->Rmask,
		src->format->Gmask,
		src->format->Bmask,
		src->format->Amask);
}

static void AllocSDLSurface(SDL_Surface **dst, SDL_Surface *src)
{
	AllocSDLSurface(dst, src->w, src->h, src);
}

static int InitSDL(FFSettings *setting)
{
	int ret = 0;
	ret = SDL_Init(SDL_INIT_AUDIO | SDL_INIT_VIDEO);

	if(ret >= 0)
	{
		int w,h;
		if(setting->v.valid)
		{
			w = setting->v.width;
			h = setting->v.height;
		}
		else
		{
			w = 200;
			h = 200;
		}
		_context->window = SDL_SetVideoMode(w, h, SDL_VIDEO_BPP, SDL_HWSURFACE | SDL_RESIZABLE);
		AllocSDLSurface(&_context->background, _context->window);
		AllocSDLSurface(&_context->video, _context->window);
		SDL_WM_SetCaption("SDL Player", NULL);
	}

	if(ret >= 0 && setting->a.valid)
	{
		int channels;
		ret = audio_open(_context, 0, setting->a.channels, setting->a.sampleRate,
			NULL, &channels);
		if(ret >= 0 && channels != setting->a.channels)
		{
			FFAudioSetting setting = {-1};
			setting.channels = channels;
			FF_SetAudioOutputSetting(_context->handle, &setting);
		}
	}

	if(ret < 0 || (!setting->v.valid && !setting->a.valid))
		ret = -1;

	return ret;
}

static DWORD WINAPI InputThread(void *)
{
	std::string s;
	SDL_Event event;
	while(std::getline(cin, s))
	{
		if(s.compare("q") == 0)
		{
			event.type = SDL_QUIT;
			SDL_PushEvent(&event);
			break;
		}
		else if(s.compare("p") == 0)
		{
			FF_Pause(_context->handle);
			SDL_PauseAudio(1);
		}
		else if(s.compare("step") == 0)
		{
			FF_ReadNextFrame(_context->handle);
		}
		else if(s.compare("r") == 0)
		{
			FF_Run(_context->handle);
			SDL_PauseAudio(0);
		}
		else
			printf("Unrecognize command '%s'\n", s.c_str());
	}
	printf("Input thread exit.\n");
	return 0;
}

static void LoopEvents(FFSettings *pSettings, void *handle)
{
	BOOL running = TRUE;

	SDL_Event event;
	int w, h;
	double r, srcR;
	if(pSettings->v.valid)
		srcR = pSettings->v.width / (double)pSettings->v.height;
	while(running && SDL_WaitEvent(&event))
	{
		switch(event.type)
		{
		case SDL_QUIT:
			running = FALSE;
			break;
		case SDL_VIDEORESIZE:
			SDL_SetVideoMode(event.resize.w, event.resize.h, SDL_VIDEO_BPP, SDL_HWSURFACE | SDL_RESIZABLE);
			AllocSDLSurface(&_context->background, _context->window);
			if(pSettings->v.valid)
			{
				w = event.resize.w;
				h = event.resize.h;
				r = w / (double)h;
				if(r > srcR)
					w = (int)(h * srcR + 0.5);
				else if(r < srcR)
					h = (int)(w / srcR + 0.5);
				w &= ~3;
				FF_SetResolution(handle, w, h);
			}
			break;
			//TODO: shortcuts, full screen etc...
		case SDL_KEYDOWN:
			break;
		default:
			break;
		}
	}
}

static int InitTFFPlayer(wchar_t *file, FFSettings *setting, void **handle)
{
	int err = FF_Init();
	if(err >= 0)
	{
		FFInitSetting initSetting = {0};
		wcscpy_s(initSetting.fileName, file);
		initSetting.framePixFmt = FF_FRAME_PIXFORMAT_RGB24;
		initSetting.sampleFmt = FF_AUDIO_SAMPLE_FMT_S16;
		initSetting.audioDisable = 0;
		initSetting.videoDisable = 0;
		initSetting.useExternalClock = 1;
		err = FF_InitHandle(&initSetting, setting, handle);
	}

	if(err >= 0)
	{
		err = FF_SetCallback(*handle, NewFrame, Finished);
	}

	if(err < 0)
		FF_CloseHandle(*handle);

	return err;
}

int player(int argc, wchar_t *argv[])
{
	void *handle;
	FFSettings settings;
	char msg[MAX_PATH] = {0};

	int err = InitTFFPlayer(argv[1], &settings, &handle);

	if(err >= 0)
	{
		_context = (SDLPlayerContext *)malloc(sizeof(SDLPlayerContext));
		memset(_context, 0, sizeof(SDLPlayerContext));
		_context->handle = handle;
		err = InitSDL(&settings);
	}

	if(err >= 0)
	{
		if(settings.v.valid)
			err = FF_Run(handle);
		if(settings.a.valid)
			SDL_PauseAudio(0);
	}

	HANDLE inputThread = CreateThread(NULL, 0, InputThread, NULL, 0, NULL);
	if(err >= 0)
		LoopEvents(&settings, handle);

	if(WaitForSingleObject(inputThread, 1000) > 0)
		TerminateThread(inputThread, 0);
	CloseHandle(inputThread);

	if(settings.a.valid)
		SDL_CloseAudio();
	if(err >= 0)
	{
		err = FF_CloseHandle(handle);

		if(_context)
		{
			if(_context->window)
				SDL_FreeSurface(_context->window);
			if(_context->overlay)
				SDL_FreeYUVOverlay(_context->overlay);
			if(_context->video)
				SDL_FreeSurface(_context->video);
			if(_context->background)
				SDL_FreeSurface(_context->background);

			free(_context);
			_context = NULL;
		}

		SDL_Quit();
	}

	cout << "exit." << endl;
	return 0;
}

int testDecodeSubtitle(wchar_t *file)
{
	AVFormatContext *ctx = NULL;
	char szFile[MAX_PATH] = {0};
	int subtitleIndex, err, gotSubtitle;
	AVSubtitle subtitle;
	wchar_t info[1024] = {0};
	av_register_all();
	WideCharToMultiByte(CP_UTF8, 0, file, -1, szFile, MAX_PATH, NULL, NULL);
	avformat_open_input(&ctx, szFile, NULL, NULL);
	av_find_stream_info(ctx);
	av_dump_format(ctx, 0, szFile, 0);
	subtitleIndex = av_find_best_stream(ctx, AVMEDIA_TYPE_SUBTITLE, -1, -1, NULL, 0);

	if(subtitleIndex < 0)
	{
		cout << "There is no subtitle stream." << endl;
		return -1;
	}

	AVStream *stream = ctx->streams[subtitleIndex];
	AVCodec *decoder = avcodec_find_decoder(stream->codec->codec_id);
	err = avcodec_open2(stream->codec, decoder, NULL);

	if(err < 0)
	{
		cout << "err when open decoder." << endl;
		return err;
	}

	AVPacket pkt;
	av_init_packet(&pkt);
	while(av_read_frame(ctx, &pkt) >= 0)
	{
		if(pkt.stream_index == subtitleIndex)
		{
			int len = avcodec_decode_subtitle2(stream->codec, &subtitle, &gotSubtitle, &pkt);
			if(len < 0)
			{
				cout << "err when decode subtitle" << endl;
				continue;
			}

			if(gotSubtitle)
			{
				char *ass = subtitle.rects[0]->ass;
				MultiByteToWideChar(CP_UTF8, 0, ass, -1, info, 1024);
			}
		}
		av_free_packet(&pkt);
	}
	avsubtitle_free(&subtitle);

	return 0;
}

void SDLTest()
{
	int ret, quit;
	SDL_Surface *screen, *bmp;
	SDL_Event event;
	ret = SDL_Init(SDL_INIT_EVERYTHING);
	int width, height;
	width = height = 400;

	screen = SDL_SetVideoMode(width, height, 0, SDL_SWSURFACE);

	SDL_Rect rect;
	rect.x = 100;
	rect.y = 100;

	bmp = SDL_CreateRGBSurface(SDL_SWSURFACE, 100, 100, screen->format->BitsPerPixel,
		screen->format->Rmask, screen->format->Gmask, screen->format->Bmask, screen->format->Amask);

	memset(bmp->pixels, 100, bmp->w * bmp->h * bmp->format->BytesPerPixel);

	SDL_BlitSurface(bmp, NULL, screen, &rect);

	SDL_Flip(screen);

	SDL_CreateCond();
	quit = 0;
	while(!quit)
	{
		SDL_Delay(50);
		while(SDL_PollEvent(&event))
		{
			switch(event.type)
			{
			case SDL_QUIT:
				quit = 1;
				break;
			case SDL_KEYDOWN:
				switch(event.key.keysym.sym)
				{
				case SDLK_RETURN:
					screen = SDL_SetVideoMode(width, height, 0, SDL_SWSURFACE |
						 SDL_FULLSCREEN);
					break;
				case SDLK_ESCAPE:
					screen = SDL_SetVideoMode(width, height, 0, SDL_SWSURFACE);
					break;
				}
				break;
			default:
				break;
			}
		}
	}

	SDL_FreeSurface(bmp);
	SDL_FreeSurface(screen);
	SDL_Quit();
}

int wmain(int argc, wchar_t *argv[])
{
	TFFLogInit("D:\\playerlog\\log.txt");
	player(argc, argv);
	//SDLTest();
	return 0;
}