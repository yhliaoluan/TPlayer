﻿#include <Windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <iostream>
#include "TFFmpeg.h"
#include "SDL.h"

using std::cout;
using std::cin;
using std::endl;

#define CONTROL_HEIGHT 100
#define SDL_AUDIO_BUFFER_SIZE 4096

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
		_context->window = SDL_SetVideoMode(w, h, 0, SDL_SWSURFACE/* | SDL_RESIZABLE*/);
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
		SDL_OpenAudio(&wantedSpec, &spec);
	}

	if(ret < 0 || (!setting->v.valid && !setting->a.valid))
		ret = -1;

	return ret;
}

static void LoopEvents(FFSettings *pSettings, void *handle)
{
	BOOL running = TRUE;

	SDL_Event event;
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
		initSetting.framePixFmt = FF_FRAME_PIXFORMAT_YUV420;
		err = FF_InitFile(&initSetting, &settings, &handle);
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
		{
			SDL_PauseAudio(0);
		}
	}

	if(err >= 0)
		LoopEvents(&settings, handle);

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

int wmain(int argc, wchar_t *argv[])
{
	player(argc, argv);
	return 0;
}