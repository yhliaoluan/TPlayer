#include "TFFmpegPlayer.h"
#include "TFFmpegUtil.h"

TFFmpegPlayer::TFFmpegPlayer() :
	_videoDecoder(NULL),
	_audioDecoder(NULL),
	_ctx(NULL),
	_pkter(NULL),
	_cmd(Player_Cmd_None),
	_thread(NULL),
	_cmdMutex(NULL),
	_cmdCond(NULL),
	_clock(CLOCK_NO_VALUE)
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
	if(_thread)
	{
		TFF_WaitThread(_thread, 1000);
		CloseThreadP(&_thread);
	}
	if(_videoDecoder)
	{
		delete _videoDecoder;
		_videoDecoder = NULL;
	}
	if(_pkter)
	{
		delete _pkter;
		_pkter = NULL;
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

int TFFmpegPlayer::Convert(FFVideoFrame *in, FFFrame *out)
{
	int ret = FF_OK;

	out->data = in->frame->data;
	out->buff = in->buffer;
	out->linesize = in->frame->linesize;
	out->size = in->size;
	out->pts = in->frame->pts;
	out->width = in->width;
	out->height = in->height;
	out->time = out->pts * av_q2d(_ctx->videoStream->time_base);
	out->duration = (int64_t)(in->frame->pkt_duration * av_q2d(_ctx->videoStream->time_base) * 1000);

	return ret;
}

void TFFmpegPlayer::ThreadStart()
{
	DebugOutput("TFFmpegPlayer::Thread thread start.");
	int ret = 0;
	BOOL holdMutex = FALSE;
	FFFrame frame = {0};
	FFVideoFrame *videoFrame = NULL;
	FFVideoFrame vf = {0};
	while(1)
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
			int64_t pos = (int64_t)(_seekTime * AV_TIME_BASE);
			AVRational timeBaseQ = {1, AV_TIME_BASE};
			pos = av_rescale_q(pos, timeBaseQ, _ctx->videoStream->time_base);
			//_videoDecoder->SeekPos(pos);
			if(_videoDecoder->Decode(&vf) != FF_EOF)
				DebugOutput("TFFmpegPlayer::Thread seek completed but cannot get a frame.");
			else
			{
				Convert(&vf, &frame);
				OnNewFrame(&frame);
				_videoDecoder->Free(&vf);
			}
		}
		else if(_cmd & Player_Cmd_Step)
		{
			DebugOutput("TFFmpegPlayer::Thread Player_Cmd_Step");
			_cmd &= ~Player_Cmd_Step;
			if(_videoDecoder->Decode(&vf) != FF_EOF)
			{
				Convert(&vf, &frame);
				OnNewFrame(&frame);
				_videoDecoder->Free(&vf);
			}
		}
		else if(_cmd & Player_Cmd_Run)
		{
			ret = _videoDecoder->Decode(&vf);
			if(ret == FF_EOF)
			{
				DebugOutput("TFFmpegPlayer::Thread end of file. ");
				_cmd &= ~Player_Cmd_Run;
				OnFinished();
			}
			else
			{
				Convert(&vf, &frame);
				OnNewFrame(&frame);
				_videoDecoder->Free(&vf);
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
	AVStream *pVS = _ctx->videoStream;
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
	if(_ctx->audioStream)
	{
		AVCodecContext *pACodecCtx = _ctx->audioStream->codec;
		pSettings->audioSampleRate = pACodecCtx->sample_rate;
		pSettings->audioChannels = pACodecCtx->channels;
	}
	strcpy_s(pSettings->codecName, pVCodecCtx->codec->long_name);
	return 0;
}

int TFFmpegPlayer::Start()
{
	_pkter->Start();
	//_videoDecoder->Start();
	//_audioDecoder->Start();
	if(!_thread)
		_thread = TFF_CreateThread(SThreadStart, this);
	return 0;
}

int TFFmpegPlayer::Init(const FFInitSetting *pSetting)
{
	int ret = InitCtx(pSetting);
	_cmdMutex = TFF_CreateMutex();
	_cmdCond = TFF_CreateCond();
	if(ret >= 0)
	{
		_pkter = new TFFmpegPacketer(_ctx);
		ret = _pkter->Init();
	}
	if(ret >= 0)
	{
		_videoDecoder = new TFFmpegVideoDecoder(_ctx, _pkter);
		ret = _videoDecoder->Init();
	}

	if(ret >= 0)
	{
		_audioDecoder = new TFFmpegAudioDecoder(_ctx, _pkter);
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
	if(!_ctx->audioStream)
		return FF_NO_AUDIO_STREAM;
	pCodec = avcodec_find_decoder(_ctx->audioStream->codec->codec_id);
	return avcodec_open2(_ctx->audioStream->codec, pCodec, NULL);
}

int TFFmpegPlayer::OpenVideoCodec()
{
	//open video codec context
	AVCodec *pCodec = avcodec_find_decoder(_ctx->videoStream->codec->codec_id);
	return avcodec_open2(_ctx->videoStream->codec, pCodec, NULL);
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
	_ctx = (FFContext *)av_mallocz(sizeof(FFContext));
	_ctx->vsIndex = -1;
	_ctx->asIndex = -1;
	_ctx->handleAudio = 1;
	_ctx->handleVideo = 1;
	_ctx->pixFmt = FF_GetAVPixFmt(pSetting->dstFramePixFmt);

	WideCharToMultiByte(
		CP_UTF8,
		0,
		pSetting->fileName,
		-1,
		szFile,
		1024,
		NULL,
		NULL);

	ret = avformat_open_input(&_ctx->pFmtCtx,
		szFile, NULL, NULL);

	if(ret < 0)
	{
		DebugOutput("avformat_open_input failed.");
		ret = FF_ERR_CANNOT_OPEN_FILE;
		return ret;
	}

	ret = avformat_find_stream_info(_ctx->pFmtCtx, NULL);

	if(ret < 0)
	{
		DebugOutput("avformat_find_stream_info failed.");
		ret = FF_ERR_CANNOT_FIND_STREAM_INFO;
		return ret;
	}

	for(unsigned int i = 0; i < _ctx->pFmtCtx->nb_streams; i++)
	{
		if(_ctx->pFmtCtx->streams[i]->codec->codec_type == AVMEDIA_TYPE_VIDEO &&
			_ctx->vsIndex < 0)
			_ctx->vsIndex = i;
		else if(_ctx->pFmtCtx->streams[i]->codec->codec_type == AVMEDIA_TYPE_AUDIO &&
			_ctx->asIndex < 0)
			_ctx->asIndex = i;
	}

	if(_ctx->vsIndex == -1)
	{
		DebugOutput("Cannot find video stream.");
		ret = FF_ERR_NO_VIDEO_STREAM;
		return ret;
	}
	else
		_ctx->videoStream = _ctx->pFmtCtx->streams[_ctx->vsIndex];

	ret = OpenVideoCodec();

	if(ret < 0)
	{
		DebugOutput("Open video codec failed.");
		ret = FF_ERR_CANNOT_OPEN_VIDEO_CODEC;
		return ret;
	}

	if(_ctx->asIndex == -1)
		DebugOutput("Cannot find audio stream.");
	else
		_ctx->audioStream = _ctx->pFmtCtx->streams[_ctx->asIndex];

	ret = OpenAudioCodec();
	if(ret < 0)
	{
		DebugOutput("Open audio codec failed.");
		ret = FF_ERR_CANNOT_OPEN_AUDIO_CODEC;
		return ret;
	}

	DebugOutput("Init stream successfully.");
	DebugOutput("Width: %d Height: %d", _ctx->videoStream->codec->width, _ctx->videoStream->codec->height);
	DebugOutput("Codec name: %s.", _ctx->videoStream->codec->codec->long_name);
	DebugOutput("Video stream index: %d", _ctx->vsIndex);
	DebugOutput("Audio stream index: %d", _ctx->asIndex);

	_ctx->width = _ctx->videoStream->codec->width;
	_ctx->height = _ctx->videoStream->codec->height;
	if(_ctx->audioStream)
	{
		_ctx->channelLayout = av_get_default_channel_layout(_ctx->audioStream->codec->channels);
		_ctx->sampleFmt = FF_GetAVSampleFmt(FF_AUDIO_SAMPLE_FMT_S16);
		_ctx->freq = _ctx->audioStream->codec->sample_rate;
	}
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
	_seekTime = time;
	CmdAndSignal(Player_Cmd_Seek);
	return 0;
}

int TFFmpegPlayer::FillAudioStream(uint8_t *stream, int len)
{
	return _audioDecoder->Fill(stream, len);
}

int TFFmpegPlayer::SetResolution(int w, int h)
{
	_videoDecoder->SetResolution(w, h);
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
	if(_ctx)
	{
		if(_ctx->videoStream)
			avcodec_close(_ctx->videoStream->codec);
		if(_ctx->audioStream)
			avcodec_close(_ctx->audioStream->codec);
		if(_ctx->pFmtCtx)
			avformat_close_input(&_ctx->pFmtCtx);
		av_freep(&_ctx);
	}
}