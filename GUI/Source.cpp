#include "..\TPlayer\TFFmpeg.h"
#include "SDL.h"
#include <Windows.h>

#undef main

static SDL_Surface *_screen;

void ShowBMP(char *file, SDL_Surface *screen, int x, int y)
{
    SDL_Surface *image;
    SDL_Rect dest;

    /* 将BMP文件加载到一个surface*/
    image = SDL_LoadBMP(file);
    if ( image == NULL ) {
        fprintf(stderr, "无法加载 %s: %s\n", file, SDL_GetError());
        return;
    }

    /* Blit到屏幕surface。onto the screen surface.
       这时不能锁住surface。
     */
    dest.x = x;
    dest.y = y;
    dest.w = image->w;
    dest.h = image->h;
    SDL_BlitSurface(image, NULL, screen, &dest);

    /* 刷新屏幕的变化部分 */
    SDL_UpdateRects(screen, 1, &dest);
}

static unsigned long __stdcall ThreadStart(void *)
{
	_screen = SDL_SetVideoMode(640,480,0,0);
	if(!_screen)
		return -1;

	ShowBMP("D:\\Picture\\test.bmp", _screen, 0, 0);
	return 0;
}

int main()
{
	int err = SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_TIMER);
	if(err < 0)
		return err;

	HANDLE thread = CreateThread(NULL, 0, ThreadStart, NULL, 0, NULL);
	CloseHandle(thread);
	getchar();

	SDL_FreeSurface(_screen);
err_end:
	return 0;
}