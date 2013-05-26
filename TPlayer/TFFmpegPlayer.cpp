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
	_finishedType(0),
	_audioWait(1),
	_flushMutex(NULL)
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
	if(_audioDecoder)
	{
		delete _audioDecoder;
		_audioDecoder = NULL;
	}
	if(_pkter)
	{
		delete _pkter;
		_pkter = NULL;
	}
	TFF_CondDestroy(&_cmdCond);
	CloseMutexP(&_cmdMutex);
	CloseMutexP(&_flushMutex);
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
			TFF_GetMutex(_flushMutex, TFF_INFINITE);
			_pkter->SeekPos(_seekTime);
			if(_ctx->videoStream)
				avcodec_flush_buffers(_ctx->videoStream->codec);
			if(_ctx->audioStream)
				avcodec_flush_buffers(_ctx->audioStream->codec);
			TFF_ReleaseMutex(_flushMutex);
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
			_clock.Start();
			if(!_videoDecoder)
			{
				_cmd &= ~Player_Cmd_Run;
				DebugOutput("Video decoder is not initialized.");
				continue;
			}
			ret = _videoDecoder->Decode(&vf);
			if(ret == FF_EOF)
			{
				DebugOutput("TFFmpegPlayer::Thread end of file. ");
				_cmd &= ~Player_Cmd_Run;
				OnFinished(TFF_PLAYTYPE_VIDEO);
			}
			else
			{
				_finishedType &= ~TFF_PLAYTYPE_VIDEO;
				Convert(&vf, &frame);
				SyncVideo(&frame);
				OnNewFrame(&frame);
				_videoDecoder->Free(&vf);
			}
		}
		else
		{
			TFF_CondWait(_cmdCond, _cmdMutex);
			holdMutex = TRUE;
		}

		if(!holdMutex)
			TFF_ReleaseMutex(_cmdMutex);
	}
	DebugOutput("TFFmpegPlayer::Thread thread exit.");
}

void TFFmpegPlayer::SyncVideo(FFFrame *f)
{
	if(f->time > 0)
	{
		long sleepMS = ((long)(f->time * 1000) - _clock.GetTime());
		if(sleepMS > 0)
			TFF_Sleep(sleepMS);
	}
}

int TFFmpegPlayer::GetVideoInfo(FFSettings *pSettings)
{
	if(_ctx->videoStream)
	{
		AVStream *pVS = _ctx->videoStream;
		AVCodecContext *pVCodecCtx = pVS->codec;
		AVRational frameRate = av_stream_get_r_frame_rate(pVS);
		pSettings->v.width = pVCodecCtx->width;
		pSettings->v.height = pVCodecCtx->height;
		pSettings->v.fpsNum = frameRate.num;
		pSettings->v.fpsDen = frameRate.den;
		pSettings->v.timebaseNum = pVS->time_base.num;
		pSettings->v.timebaseDen = pVS->time_base.den;
		pSettings->v.totalFrames = pVS->nb_frames;
		pSettings->v.duration = pVS->duration;
		pSettings->v.valid = 1;
		strcpy_s(pSettings->v.codecName, pVCodecCtx->codec->long_name);
	}
	else
		pSettings->v.valid = 0;
	if(_ctx->audioStream)
	{
		AVCodecContext *pACodecCtx = _ctx->audioStream->codec;
		pSettings->a.sampleRate = pACodecCtx->sample_rate;
		pSettings->a.channels = pACodecCtx->channels;
		pSettings->a.valid = 1;
	}
	else
		pSettings->a.valid = 0;
	return 0;
}

int TFFmpegPlayer::Start()
{
	_pkter->Start();
	if(!_thread)
		_thread = TFF_CreateThread(SThreadStart, this);
	return 0;
}

int TFFmpegPlayer::Init(const FFInitSetting *pSetting)
{
	int ret = InitCtx(pSetting);
	_flushMutex = TFF_CreateMutex();
	_cmdMutex = TFF_CreateMutex();
	_cmdCond = TFF_CondCreate();
	if(ret >= 0)
	{
		_pkter = new TFFmpegPacketer(_ctx);
		ret = _pkter->Init();
	}
	if(ret >= 0 && _ctx->videoStream)
	{
		_videoDecoder = new TFFmpegVideoDecoder(_ctx, _pkter);
		ret = _videoDecoder->Init();
	}

	if(ret >= 0 && _ctx->audioStream)
	{
		_audioDecoder = new TFFmpegAudioDecoder(_ctx, _pkter);
		ret = _audioDecoder->Init();
	}

	_cmd |= Player_Cmd_Stop;
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
	if(!_ctx->videoStream)
		return FF_NO_VIDEO_STREAM;
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
	_ctx->pixFmt = FF_GetAVPixFmt(pSetting->framePixFmt);

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

	av_dump_format(_ctx->pFmtCtx, 0, szFile, 0);

	_ctx->vsIndex = av_find_best_stream(_ctx->pFmtCtx, AVMEDIA_TYPE_VIDEO, _ctx->vsIndex, -1,
		NULL, 0);

	if(_ctx->vsIndex < 0)
	{
		DebugOutput("Cannot find video stream.");
	}
	else
	{
		_ctx->videoStream = _ctx->pFmtCtx->streams[_ctx->vsIndex];
		ret = OpenVideoCodec();
		if(ret < 0)
		{
			DebugOutput("Open video codec failed.");
			ret = FF_ERR_CANNOT_OPEN_VIDEO_CODEC;
			return ret;
		}
	}

	_ctx->asIndex = av_find_best_stream(_ctx->pFmtCtx, AVMEDIA_TYPE_AUDIO, _ctx->asIndex,
		_ctx->vsIndex, NULL, 0);

	if(_ctx->asIndex < 0)
		DebugOutput("Cannot find audio stream.");
	else
	{
		_ctx->audioStream = _ctx->pFmtCtx->streams[_ctx->asIndex];
		ret = OpenAudioCodec();
		if(ret < 0)
		{
			DebugOutput("Open audio codec failed.");
			ret = FF_ERR_CANNOT_OPEN_AUDIO_CODEC;
			return ret;
		}
	}

	DebugOutput("Init stream successfully.");
	DebugOutput("Video stream index: %d", _ctx->vsIndex);
	DebugOutput("Audio stream index: %d", _ctx->asIndex);
	if(_ctx->videoStream)
	{
		DebugOutput("Video information:");
		DebugOutput("Width: %d Height: %d", _ctx->videoStream->codec->width, _ctx->videoStream->codec->height);
		DebugOutput("Codec name: %s", _ctx->videoStream->codec->codec->long_name);
		_ctx->width = _ctx->videoStream->codec->width;
		_ctx->height = _ctx->videoStream->codec->height;
	}
	if(_ctx->audioStream)
	{
		DebugOutput("Audio information:");
		_ctx->channelLayout = av_get_default_channel_layout(_ctx->audioStream->codec->channels);
		_ctx->sampleFmt = FF_GetAVSampleFmt(FF_AUDIO_SAMPLE_FMT_S16);
		_ctx->freq = _ctx->audioStream->codec->sample_rate;
		DebugOutput("Sample rate: %d MHz", _ctx->freq);
		DebugOutput("Stream time base %d/%d", _ctx->audioStream->time_base.num, _ctx->audioStream->time_base.den);
		char channelStr[MAX_PATH] = {0};
		av_get_channel_layout_string(channelStr, MAX_PATH, _ctx->audioStream->codec->channels, _ctx->channelLayout);
		DebugOutput("Channles: %d", _ctx->audioStream->codec->channels);
		DebugOutput("Channel layout: %s", channelStr);
		DebugOutput("Codec name :%s", _ctx->audioStream->codec->codec->long_name);
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
	int ret = FF_OK;
	int64_t pts;
	TFF_GetMutex(_flushMutex, TFF_INFINITE);
	if((ret = _audioDecoder->Fill(stream, len, &pts)) >= 0 && ret != FF_EOF)
	{
		long ms = (long)(pts * 1000 * av_q2d(_ctx->audioStream->time_base));
		if(_audioWait)
		{
			long sleepMS = ms - _clock.GetTime();
			if(sleepMS > 0)
			{
				DebugOutput("Audio will start in %d ms.", sleepMS);
				TFF_Sleep(sleepMS);
			}
			_audioWait = 0;
		}
		else
			_clock.Sync(ms);
	}
	else if(ret == FF_EOF)
	{
		TFF_GetMutex(_cmdMutex, TFF_INFINITE);
		OnFinished(TFF_PLAYTYPE_AUDIO);
		TFF_ReleaseMutex(_cmdMutex);
	}
	TFF_ReleaseMutex(_flushMutex);
	return ret;
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

void TFFmpegPlayer::OnFinished(int type)
{
	_finishedType |= type;
	if((!_ctx->videoStream || (_finishedType & TFF_PLAYTYPE_VIDEO)) &&
		(!_ctx->audioStream || (_finishedType & TFF_PLAYTYPE_AUDIO)) && _fFinishedCB)
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