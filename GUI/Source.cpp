#include <Windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string>
#include <iostream>
#include "TFFmpeg.h"
#include "SDL.h"
#include "SDL_opengl.h"

using std::cout;
using std::cin;
using std::endl;

#define CONTROL_HEIGHT 100
#define SDL_AUDIO_BUFFER_SIZE 1024

typedef struct _st_PlayContext
{
	SDL_Surface *window;
	SDL_Overlay *overlay;
	void *handle; //player handle
} SDLPlayerContext;

static SDLPlayerContext *_context;

void __stdcall NewFrame(FFFrame *p)
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

static int audio_open(void *opaque, int64_t wanted_channel_layout, int wanted_nb_channels, int wanted_sample_rate)
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

    return spec.size;
}

static int InitSDL(FFSettings *setting)
{
	int ret = 0;
	ret = SDL_Init(SDL_INIT_EVERYTHING);

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
		_context->window = SDL_SetVideoMode(w, h, 0, SDL_HWSURFACE/* | SDL_RESIZABLE*/);
		_context->overlay = SDL_CreateYUVOverlay(w, h, SDL_IYUV_OVERLAY, _context->window);
		SDL_WM_SetCaption("SDL Player", NULL);
	}

	if(ret >= 0 && setting->a.valid)
	{
		SDL_AudioSpec wantedSpec;
		wantedSpec.freq = setting->a.sampleRate;
		wantedSpec.format = AUDIO_S16SYS;
		wantedSpec.channels = setting->a.channels;
		wantedSpec.silence = 0;
		wantedSpec.samples = SDL_AUDIO_BUFFER_SIZE;
		wantedSpec.callback = AudioCallback;
		wantedSpec.userdata = _context;

		SDL_AudioSpec spec;
		ret = SDL_OpenAudio(&wantedSpec, &spec);

		if(ret < 0)
		{
			printf("%s\n", SDL_GetError());
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
		printf("Unrecognize command '%s'\n", s.c_str());
	}
	printf("Input thread exit.\n");
	return 0;
}

static void LoopEvents(FFSettings *pSettings, void *handle)
{
	BOOL running = TRUE;

	SDL_Event event;
	while(running && SDL_WaitEvent(&event))
	{
		switch(event.type)
		{
		case SDL_QUIT:
			running = FALSE;
			break;
			//TODO: handle resize event.
		case SDL_VIDEORESIZE:
			/*w = event.resize.w;
			h = event.resize.h;
			r = w / (double)h;
			if(r > srcR)
				w = (int)(h * srcR + 0.5);
			else if(r < srcR)
				h = (int)(w / srcR + 0.5);
			if(w % 2 != 0)
				w -= 1;
			if(h % 2 != 0)
				h -= 1;
			cout << "Resize w:" << w << " h:" << h << endl;
			FF_SetResolution(handle, w, h);*/
			break;
			//TODO: shorcuts, full screen etc...
		case SDL_KEYDOWN:
			break;
		default:
			break;
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
		initSetting.framePixFmt = FF_FRAME_PIXFORMAT_YUV420;
		initSetting.audioDisable = 0;
		initSetting.videoDisable = 0;
		initSetting.useExternalClock = 1;
		err = FF_InitHandle(&initSetting, &settings, &handle);
	}

	if(err >= 0)
	{
		err = FF_SetCallback(handle, NewFrame, Finished);
	}

	if(err >= 0)
	{
		_context = (SDLPlayerContext *)malloc(sizeof(SDLPlayerContext));
		memset(_context, 0, sizeof(SDLPlayerContext));
		err = InitSDL(&settings);
		_context->handle = handle;
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
	player(argc, argv);
	//SDLTest();
	return 0;
}