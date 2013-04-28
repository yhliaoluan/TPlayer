#ifndef _T_FFMPEG_H_
#define _T_FFMPEG_H_

#include "TFFmpegDef.h"
#include "TFFmpegPacketer.h"
#include "TFFmpegDecoder.h"
#include "TFFmpegPlayer.h"

int __stdcall FF_Init();

int __stdcall FF_InitFile(const wchar_t *fileName,
						  OUT FFSettings *pSettings,
						  OUT void **ppHandle);

int __stdcall FF_CloseHandle(void *pHandle);

int __stdcall FF_SetCallback(void *p, NewFrameCB, FinishedCB);

int __stdcall FF_Run(void *p);
int __stdcall FF_Pause(void *p);
int __stdcall FF_Stop(void *p);
int __stdcall FF_SeekTime(void *p, double time);
int __stdcall FF_GetCurFrame(void *p);
int __stdcall FF_ReadNextFrame(void *p);

int __stdcall FF_Uninit();

#endif