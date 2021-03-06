#ifndef _T_FFMPEG_H_
#define _T_FFMPEG_H_

#include "TFFmpegDef.h"
#include "TFFmpegPacketer.h"
#include "TFFmpegVideoDecoder.h"
#include "TFFmpegPlayer.h"

int __stdcall FF_Init();

int __stdcall FF_InitHandle(const FF_INIT_SETTING *pInitSetting,
						  OUT FF_SETTINGS *pSettings,
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

int __stdcall FF_ScalePrepared(int srcW,
							   int srcH,
							   int dstW,
							   int dstH,
							   OUT void **ppCtx);

int __stdcall FF_Scale(void *pCtx,
					   unsigned char *buff,
					   int srcStride,
					   int srcH,
					   unsigned char *outBuff,
					   int dstStride);

int __stdcall FF_SetResolution(void *p, int width, int height);

int __stdcall FF_CopyAudioStream(void *p, uint8_t *stream, int len);
int __stdcall FF_SetAudioOutputSetting(void *p, FF_AUDIO_SETTING *setting);

#endif