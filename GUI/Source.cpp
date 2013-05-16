#include <Windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <iostream>
#include "TFFmpeg.h"
#include "SDL.h"

using std::cout;
using std::cin;
using std::endl;

static SDL_Surface *_screen;
static SDL_Overlay *_bmp;

static int _beginTime;

void __stdcall NewFrame(FFFrame *p)
{
	SDL_Rect rect;
	//cout << "New frame. time:" << p->time;
	//cout << " width:" << p->width;
	//cout << " height:" << p->height;
	//cout << " size:" << p->size << endl;

	int elMS = SDL_GetTicks() - _beginTime;
	int showMS = (int)(p->time * 1000);
	if(showMS > elMS)
	{
		//cout << "Delay " << (showMS - elMS) << endl;
		SDL_Delay(showMS - elMS);
	}
	int err = SDL_LockYUVOverlay(_bmp);

	int linesize = p->width * p->height;
	memcpy(_bmp->pixels[0], p->data[0], linesize);
	memcpy(_bmp->pixels[1], p->data[1], linesize >> 2);
	memcpy(_bmp->pixels[2], p->data[2], linesize >> 2);

	SDL_UnlockYUVOverlay(_bmp);
	rect.x = 0;
	rect.y = 0;
	rect.w = p->width;
	rect.h = p->height;
	SDL_DisplayYUVOverlay(_bmp, &rect);
	SDL_Flip(_screen);
}

void __stdcall Finished(void)
{
	cout << "Finished." << endl;
}

static int InitSDL(int width, int height)
{
	int ret = 0;
	ret = SDL_Init(SDL_INIT_EVERYTHING);

	_screen = SDL_SetVideoMode(width, height, 0, SDL_SWSURFACE);

	_bmp = SDL_CreateYUVOverlay(width, height, SDL_IYUV_OVERLAY, _screen);

	return 0;
}

int wmain(int argc, wchar_t *argv[])
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
		//initSetting.dstFramePixFmt = 0;
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
		InitSDL(settings.width, settings.height);
	}

	_beginTime = SDL_GetTicks();
	FF_Run(handle);

	BOOL running = TRUE;

	SDL_Event event; 
	while(running)
	{
		SDL_Delay(50);
		while ( SDL_PollEvent( &event ) )
		{ 
		  if  (event.type == SDL_QUIT)
		  {
			running = FALSE ;
		  }
		}
	}

	//if(err >= 0)
	//{
	//	cout << "input msg:" << endl;
	//	while(cin >> msg)
	//	{
	//		if(strcmp(msg, "q") == 0)
	//		{
	//			break;
	//		}
	//		else if(strcmp(msg, "p") == 0)
	//		{
	//			FF_Pause(handle);
	//		}
	//		else if(strcmp(msg, "run") == 0)
	//		{
	//			_beginTime = SDL_GetTicks();
	//			FF_Run(handle);
	//		}
	//		else if(strcmp(msg, "stop") == 0)
	//		{
	//			FF_Stop(handle);
	//		}
	//		else if(strcmp(msg, "step") == 0)
	//		{
	//			FF_ReadNextFrame(handle);
	//		}
	//		else if(strncmp(msg, "seek", 4) == 0)
	//		{
	//			double pos = atof(msg + 4);
	//			FF_SeekTime(handle, pos);
	//		}
	//		else if(strncmp(msg, "r", 1) == 0)
	//		{
	//			int width;
	//			int height;
	//			cin >> width;
	//			cin >> height;
	//			FF_SetResolution(handle, width, height);
	//		}
	//	}
	//}

	if(err >= 0)
	{
		err = FF_CloseHandle(handle);
	}

	if(_screen)
		SDL_FreeSurface(_screen);

	if(_bmp)
		SDL_FreeYUVOverlay(_bmp);

	if(err >= 0)
	{
		cout << "exit." << endl;
	}
	return 0;
}