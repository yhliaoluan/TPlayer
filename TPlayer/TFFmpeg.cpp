#include "TFFmpeg.h"

extern "C"
{
#include <libavformat\avformat.h>
#include <libswscale\swscale.h>
}

int __stdcall FF_Init()
{
	av_register_all();
	return 0;
}

int __stdcall FF_Uninit()
{
	return 0;
}

int __stdcall FF_InitFile(const FFInitSetting *pInitSetting,
						  OUT FFSettings *pSettings,
						  OUT void **ppHandle)
{
	int ret = 0;
	TFFmpegPlayer *player = NULL;

	player = new TFFmpegPlayer();
	ret = player->Init(pInitSetting);
	if(ret < 0)
		goto err_end;

	if(pSettings)
		player->GetVideoInfo(pSettings);

	player->Start();

	goto end;

err_end:
	FF_CloseHandle(player);
	*ppHandle = NULL;
	return ret;
end:
	*ppHandle = player;
	return ret;
}

int __stdcall FF_SetCallback(void *p, NewFrameCB p1, FinishedCB p2)
{
	TFFmpegPlayer *pPlayer = (TFFmpegPlayer *)p;
	pPlayer->SetCB(p1, p2);
	return 0;
}

int __stdcall FF_SeekTime(void *p, double time)
{
	TFFmpegPlayer *pPlayer = (TFFmpegPlayer *)p;
	pPlayer->Seek(time);
	return 0;
}

int __stdcall FF_Run(void *p)
{
	TFFmpegPlayer *pPlayer = (TFFmpegPlayer *)p;
	pPlayer->Run();
	return 0;
}
int __stdcall FF_Pause(void *p)
{
	TFFmpegPlayer *pPlayer = (TFFmpegPlayer *)p;
	pPlayer->Pause();
	return 0;
}
int __stdcall FF_Stop(void *p)
{
	TFFmpegPlayer *pPlayer = (TFFmpegPlayer *)p;
	pPlayer->Stop();
	return 0;
}

int __stdcall FF_ReadNextFrame(void *p)
{
	TFFmpegPlayer *pPlayer = (TFFmpegPlayer *)p;
	return pPlayer->Step();
}

int __stdcall FF_GetCurFrame(void *p)
{
	TFFmpegPlayer *pPlayer = (TFFmpegPlayer *)p;
	return pPlayer->GetCurFrame();
}

int __stdcall FF_CloseHandle(void *p)
{
	TFFmpegPlayer *player = (TFFmpegPlayer *)p;
	if(player)
		delete player;

	return 0;
}

int __stdcall FF_ScalePrepared(int srcW,
							   int srcH,
							   int dstW,
							   int dstH,
							   OUT void **ppCtx)
{
	SwsContext *context = sws_getCachedContext(NULL,
		srcW,
		srcH,
		PIX_FMT_BGR24,
		dstW,
		dstH,
		PIX_FMT_BGR24,
		SWS_POINT, NULL, NULL, NULL);

	if(!context)
		return -1;

	*ppCtx = context;
	return 0;
}

int __stdcall FF_Scale(void *pCtx,
					   unsigned char *buff,
					   int srcStride,
					   int srcH,
					   unsigned char *outBuff,
					   int dstStride)
{
	SwsContext *context = (SwsContext *)pCtx;
	int h = sws_scale(context,
		&buff,
		&srcStride,
		0,
		srcH,
		&outBuff,
		&dstStride);
	return h;
}

int __stdcall FF_SetResolution(void *p, int width, int height)
{
	TFFmpegPlayer *player = (TFFmpegPlayer *)p;
	if(player)
		player->SetResolution(width, height);
	return 0;
}

int __stdcall FF_PopAudioFrame(void *p, FFFrame *frame)
{
	TFFmpegPlayer *player = (TFFmpegPlayer *)p;
	if(player)
		return player->PopAudioFrame(frame);
	return FF_ERR_NOPOINTER;
}
int __stdcall FF_FreeAudioFrame(void *p, FFFrame *frame)
{
	TFFmpegPlayer *player = (TFFmpegPlayer *)p;
	if(player)
		return player->FreeAudioFrame(frame);
	return FF_ERR_NOPOINTER;
}