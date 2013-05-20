#include "TFFmpegPlayer.h"
#include "TFFmpegUtil.h"

TFFmpegPlayer::TFFmpegPlayer() :
	_pDecoder(NULL),
	_audioDecoder(NULL),
	_pCtx(NULL),
	_pPkter(NULL),
	_cmd(Player_Cmd_None),
	_hThread(NULL),
	_cmdMutex(NULL),
	_cmdCond(NULL)
{
}

TFFmpegPlayer::~TFFmpegPlayer()
{
	Uninit();
}

void TFFmpegPlayer::Uninit()
{
	_cmd |= Player_Cmd_Exit;
	TFF_CondBroadcast(_cmdCond);
	if(_hThread)
	{
		TFF_WaitThread(_hThread, 1000);
		CloseThreadP(&_hThread);
	}
	if(_pDecoder)
	{
		delete _pDecoder;
		_pDecoder = NULL;
	}
	if(_pPkter)
	{
		delete _pPkter;
		_pPkter = NULL;
	}
	if(_audioDecoder)
	{
		delete _audioDecoder;
		_audioDecoder = NULL;
	}
	TFF_DestroyCond(&_cmdCond);
	CloseMutexP(&_cmdMutex);
	FreeCtx();
}

unsigned long __stdcall TFFmpegPlayer::SThreadStart(void *p)
{
	TFFmpegPlayer *pThis = (TFFmpegPlayer *)p;
	pThis->ThreadStart();
	return 0;
}

void TFFmpegPlayer::ThreadStart()
{
	DebugOutput("TFFmpegPlayer::Thread thread start.");
	int ret = 0;
	BOOL holdMutex = FALSE;
	FFFrame frame = {0};
	FFFrameList *pFrame = NULL;
	while(true)
	{
		if(holdMutex)
			holdMutex = FALSE;
		else
			TFF_GetMutex(_cmdMutex, TFF_INFINITE);

		if(_cmd & Player_Cmd_Exit)
		{
			DebugOutput("TFFmpegPlayer::Thread Player_Cmd_Exit");
			TFF_ReleaseMutex(_cmdMutex);
			break;
		}
		else if(_cmd & Player_Cmd_Stop)
		{
			DebugOutput("TFFmpegPlayer::Thread Player_Cmd_Stop");
			_cmd &= ~Player_Cmd_Stop;
			_cmd &= ~Player_Cmd_Run;
		}
		else if(_cmd & Player_Cmd_Pause)
		{
			DebugOutput("TFFmpegPlayer::Thread Player_Cmd_Pause");
			_cmd &= ~Player_Cmd_Pause;
			_cmd &= ~Player_Cmd_Run;
		}
		else if(_cmd & Player_Cmd_Seek)
		{
			DebugOutput("TFFmpegPlayer::Thread Player_Cmd_Seek");
			_cmd &= ~Player_Cmd_Seek;
			_pDecoder->SeekPos(_seekPos);
			if(PopOneFrame(&frame, &pFrame) == FF_EOF)
				DebugOutput("TFFmpegPlayer::Thread seek completed but cannot get a frame.");
			else
			{
				OnNewFrame(&frame);
				_pDecoder->FreeSingleFrameList(&pFrame);
			}
		}
		else if(_cmd & Player_Cmd_Step)
		{
			DebugOutput("TFFmpegPlayer::Thread Player_Cmd_Step");
			_cmd &= ~Player_Cmd_Step;
			if(PopOneFrame(&frame, &pFrame) != FF_EOF)
			{
				OnNewFrame(&frame);
				_pDecoder->FreeSingleFrameList(&pFrame);
			}
		}
		else if(_cmd & Player_Cmd_Run)
		{
			ret = PopOneFrame(&frame, &pFrame);
			if(ret == FF_EOF)
			{
				DebugOutput("TFFmpegPlayer::Thread end of file. ");
				_cmd &= ~Player_Cmd_Run;
				OnFinished();
			}
			else
			{
				OnNewFrame(&frame);
				_pDecoder->FreeSingleFrameList(&pFrame);
			}
		}
		else
		{
			TFF_WaitCond(_cmdCond, _cmdMutex);
			holdMutex = TRUE;
		}

		if(!holdMutex)
			TFF_ReleaseMutex(_cmdMutex);
	}
	DebugOutput("TFFmpegPlayer::Thread thread exit.");
}

int TFFmpegPlayer::GetVideoInfo(FFSettings *pSettings)
{
	AVStream *pVS = _pCtx->videoStream;
	AVCodecContext *pVCodecCtx = pVS->codec;
	AVRational frameRate = av_stream_get_r_frame_rate(pVS);
	pSettings->width = pVCodecCtx->width;
	pSettings->height = pVCodecCtx->height;
	pSettings->fpsNum = frameRate.num;
	pSettings->fpsDen = frameRate.den;
	pSettings->timebaseNum = pVS->time_base.num;
	pSettings->timebaseDen = pVS->time_base.den;
	pSettings->totalFrames = pVS->nb_frames;
	pSettings->duration = pVS->duration;
	if(_pCtx->audioStream)
	{
		AVCodecContext *pACodecCtx = _pCtx->audioStream->codec;
		pSettings->audioSampleRate = pACodecCtx->sample_rate;
		pSettings->audioChannels = pACodecCtx->channels;
	}
	strcpy_s(pSettings->codecName, pVCodecCtx->codec->long_name);
	return 0;
}

int TFFmpegPlayer::Start()
{
	_pPkter->Start();
	_pDecoder->Start();
	_audioDecoder->Start();
	if(!_hThread)
		_hThread = TFF_CreateThread(SThreadStart, this);
	return 0;
}

int TFFmpegPlayer::Init(const FFInitSetting *pSetting)
{
	int ret = InitCtx(pSetting);
	_cmdMutex = TFF_CreateMutex();
	_cmdCond = TFF_CreateCond();
	if(ret >= 0)
	{
		_pPkter = new TFFmpegPacketer(_pCtx);
		ret = _pPkter->Init();
	}
	if(ret >= 0)
	{
		_pDecoder = new TFFmpegVideoDecoder(_pCtx, _pPkter);
		ret = _pDecoder->Init();

	}

	if(ret >= 0)
	{
		_audioDecoder = new TFFmpegAudioDecoder(_pCtx, _pPkter);
		_audioDecoder->Init();
		_cmd |= Player_Cmd_Stop;
	}
	if(ret < 0)
		Uninit();

	return ret;
}

int TFFmpegPlayer::OpenAudioCodec()
{
	AVCodec *pCodec = NULL;
	if(!_pCtx->audioStream)
		return FF_NO_AUDIO_STREAM;
	pCodec = avcodec_find_decoder(_pCtx->audioStream->codec->codec_id);
	return avcodec_open2(_pCtx->audioStream->codec, pCodec, NULL);
}

int TFFmpegPlayer::OpenVideoCodec()
{
	//open video codec context
	AVCodec *pCodec = avcodec_find_decoder(_pCtx->videoStream->codec->codec_id);
	return avcodec_open2(_pCtx->videoStream->codec, pCodec, NULL);
}

int TFFmpegPlayer::InitCtx(const FFInitSetting *pSetting)
{
	int ret = 0;
	char szFile[1024] = {0};

	if(!pSetting)
	{
		DebugOutput("Init setting is null.");
		ret = FF_ERR_NOPOINTER;
		return ret;
	}

	FreeCtx();
	_pCtx = (FFContext *)av_mallocz(sizeof(FFContext));
	_pCtx->vsIndex = -1;
	_pCtx->asIndex = -1;
	_pCtx->handleAudio = 1;
	_pCtx->handleVideo = 1;
	_pCtx->dstPixFmt = FF_GetAVPixFmt(pSetting->dstFramePixFmt);

	WideCharToMultiByte(
		CP_UTF8,
		0,
		pSetting->fileName,
		-1,
		szFile,
		1024,
		NULL,
		NULL);

	ret = avformat_open_input(&_pCtx->pFmtCtx,
		szFile, NULL, NULL);

	if(ret < 0)
	{
		DebugOutput("avformat_open_input failed.");
		ret = FF_ERR_CANNOT_OPEN_FILE;
		return ret;
	}

	ret = avformat_find_stream_info(_pCtx->pFmtCtx, NULL);

	if(ret < 0)
	{
		DebugOutput("avformat_find_stream_info failed.");
		ret = FF_ERR_CANNOT_FIND_STREAM_INFO;
		return ret;
	}

	for(unsigned int i = 0; i < _pCtx->pFmtCtx->nb_streams; i++)
	{
		if(_pCtx->pFmtCtx->streams[i]->codec->codec_type == AVMEDIA_TYPE_VIDEO &&
			_pCtx->vsIndex < 0)
			_pCtx->vsIndex = i;
		else if(_pCtx->pFmtCtx->streams[i]->codec->codec_type == AVMEDIA_TYPE_AUDIO &&
			_pCtx->asIndex < 0)
			_pCtx->asIndex = i;
	}

	if(_pCtx->vsIndex == -1)
	{
		DebugOutput("Cannot find video stream.");
		ret = FF_ERR_NO_VIDEO_STREAM;
		return ret;
	}
	else
		_pCtx->videoStream = _pCtx->pFmtCtx->streams[_pCtx->vsIndex];

	ret = OpenVideoCodec();

	if(ret < 0)
	{
		DebugOutput("Open video codec failed.");
		ret = FF_ERR_CANNOT_OPEN_VIDEO_CODEC;
		return ret;
	}

	if(_pCtx->asIndex == -1)
		DebugOutput("Cannot find audio stream.");
	else
		_pCtx->audioStream = _pCtx->pFmtCtx->streams[_pCtx->asIndex];

	ret = OpenAudioCodec();
	if(ret < 0)
	{
		DebugOutput("Open audio codec failed.");
		ret = FF_ERR_CANNOT_OPEN_AUDIO_CODEC;
		return ret;
	}

	DebugOutput("Init stream successfully.");
	DebugOutput("Width: %d Height: %d", _pCtx->videoStream->codec->width, _pCtx->videoStream->codec->height);
	DebugOutput("Codec name: %s.", _pCtx->videoStream->codec->codec->long_name);
	DebugOutput("Video stream index: %d", _pCtx->vsIndex);
	DebugOutput("Audio stream index: %d", _pCtx->asIndex);

	_pCtx->dstWidth = _pCtx->videoStream->codec->width;
	_pCtx->dstHeight = _pCtx->videoStream->codec->height;
	return ret;
}

inline void 
TFFmpegPlayer::CmdAndSignal(int cmd)
{
	TFF_GetMutex(_cmdMutex, TFF_INFINITE);
	_cmd |= cmd;
	TFF_CondBroadcast(_cmdCond);
	TFF_ReleaseMutex(_cmdMutex);
}

int TFFmpegPlayer::GetCurFrame()
{
	CmdAndSignal(Player_Cmd_Get);
	return 0;
}

int TFFmpegPlayer::Step()
{
	CmdAndSignal(Player_Cmd_Step);
	return 0;
}

int TFFmpegPlayer::Run()
{
	CmdAndSignal(Player_Cmd_Run);
	return 0;
}

int TFFmpegPlayer::Pause()
{
	CmdAndSignal(Player_Cmd_Pause);
	return 0;
}

int TFFmpegPlayer::Stop()
{
	CmdAndSignal(Player_Cmd_Stop);
	return 0;
}

int TFFmpegPlayer::Seek(double time)
{
	_seekPos = (int64_t)(time * AV_TIME_BASE);
	AVRational timeBaseQ = {1, AV_TIME_BASE};
	_seekPos = av_rescale_q(_seekPos, timeBaseQ, _pCtx->videoStream->time_base);
	CmdAndSignal(Player_Cmd_Seek);
	return 0;
}

int TFFmpegPlayer::PopOneFrame(FFFrame *frame, FFFrameList **ppFrame)
{
	int ret = 0;
	FFFrameList *pFrameList = NULL;
	if((ret = _pDecoder->Pop(&pFrameList)) < 0)
	{
		*ppFrame = NULL;
		return FF_EOF;
	}
	frame->data = pFrameList->pFrame->data;
	frame->buff = pFrameList->buffer;
	frame->linesize = pFrameList->pFrame->linesize;
	frame->size = pFrameList->size;
	frame->dts = pFrameList->pFrame->pkt_dts;
	frame->width = pFrameList->width;
	frame->height = pFrameList->height;
	frame->time = frame->dts * av_q2d(_pCtx->videoStream->time_base);
	*ppFrame = pFrameList;
	return 0;
}

int TFFmpegPlayer::SetResolution(int w, int h)
{
	_pDecoder->SetResolution(w, h);
	return 0;
}

int TFFmpegPlayer::SetCB(NewFrameCB f1, FinishedCB f2)
{
	_fNewFrameCB = f1;
	_fFinishedCB = f2;
	return 0;
}

void TFFmpegPlayer::OnNewFrame(FFFrame *p)
{
	if(_fNewFrameCB)
		_fNewFrameCB(p);
}

void TFFmpegPlayer::OnFinished(void)
{
	if(_fFinishedCB)
		_fFinishedCB();
}

void TFFmpegPlayer::FreeCtx(void)
{
	if(_pCtx)
	{
		if(_pCtx->videoStream)
			avcodec_close(_pCtx->videoStream->codec);
		if(_pCtx->audioStream)
			avcodec_close(_pCtx->audioStream->codec);
		if(_pCtx->pFmtCtx)
			avformat_close_input(&_pCtx->pFmtCtx);
		av_freep(&_pCtx);
	}
}