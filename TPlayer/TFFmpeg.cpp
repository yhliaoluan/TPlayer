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

int __stdcall FF_InitFile(const wchar_t *fileName,
						  OUT FFSettings *pSettings,
						  OUT void **ppHandle)
{
	int ret = 0;
	TFFmpegPlayer *player = new TFFmpegPlayer();
	ret = player->Init(fileName);
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