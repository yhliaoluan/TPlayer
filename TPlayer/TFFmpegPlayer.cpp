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
		if(TFF_WaitThread(_thread, 1000) > 0)
			TFFLog(TFF_LOG_LEVEL_WARNING, "Wait for player thread exit time out.");
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
	out->linesize = in->frame->linesize;
	out->pts = in->frame->pts;
	out->width = in->width;
	out->height = in->height;
	out->time = out->pts * av_q2d(_ctx->videoStream->time_base);
	out->duration = (int64_t)(in->frame->pkt_duration * av_q2d(_ctx->videoStream->time_base) * 1000);

	return ret;
}

void TFFmpegPlayer::ThreadStart()
{
	TFFLog(TFF_LOG_LEVEL_INFO, "TFFmpegPlayer::Thread thread start.");
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
			TFFLog(TFF_LOG_LEVEL_DEBUG, "TFFmpegPlayer::Thread Player_Cmd_Exit");
			TFF_ReleaseMutex(_cmdMutex);
			break;
		}
		else if(_cmd & Player_Cmd_Stop)
		{
			TFFLog(TFF_LOG_LEVEL_DEBUG, "TFFmpegPlayer::Thread Player_Cmd_Stop");
			_cmd &= ~Player_Cmd_Stop;
			_cmd &= ~Player_Cmd_Run;
		}
		else if(_cmd & Player_Cmd_Pause)
		{
			TFFLog(TFF_LOG_LEVEL_DEBUG, "TFFmpegPlayer::Thread Player_Cmd_Pause");
			_cmd &= ~Player_Cmd_Pause;
			_cmd &= ~Player_Cmd_Run;
		}
		else if(_cmd & Player_Cmd_Seek)
		{
			TFFLog(TFF_LOG_LEVEL_DEBUG, "TFFmpegPlayer::Thread Player_Cmd_Seek");
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
			TFFLog(TFF_LOG_LEVEL_DEBUG, "TFFmpegPlayer::Thread Player_Cmd_Step");
			_cmd &= ~Player_Cmd_Step;
			if(_videoDecoder)
			{
				if(_videoDecoder->Decode(&vf) != FF_EOF)
				{
					Convert(&vf, &frame);
					OnNewFrame(&frame);
					_videoDecoder->Free(&vf);
				}
			}
		}
		else if(_cmd & Player_Cmd_Run)
		{
			_clock.Start();
			if(!_videoDecoder)
			{
				_cmd &= ~Player_Cmd_Run;
				TFFLog(TFF_LOG_LEVEL_DEBUG, "Video decoder is not initialized.");
			}
			else
			{
				ret = _videoDecoder->Decode(&vf);
				if(ret == FF_EOF)
				{
					TFFLog(TFF_LOG_LEVEL_DEBUG, "TFFmpegPlayer::Thread end of file. ");
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
		}
		else
		{
			TFFLog(TFF_LOG_LEVEL_DEBUG, "TFFmpegPlayer::Thread wait on cmd cond.");
			TFF_CondWait(_cmdCond, _cmdMutex);
			TFFLog(TFF_LOG_LEVEL_DEBUG, "TFFmpegPlayer::Thread awake from cmd cond.");
			holdMutex = TRUE;
		}

		if(!holdMutex)
			TFF_ReleaseMutex(_cmdMutex);
	}
	TFFLog(TFF_LOG_LEVEL_INFO, "TFFmpegPlayer::Thread thread exit.");
}

void TFFmpegPlayer::SyncVideo(FFFrame *f)
{
	if(_useExternalClock && f->time > 0)
	{
		long sleepMS = ((long)(f->time * 1000) - _clock.GetTime());
		if(sleepMS > 0)
		{
			if(sleepMS > 1000)
				TFFLog(TFF_LOG_LEVEL_WARNING, "Sleep ms is too long. %d", sleepMS);
			TFF_Sleep(sleepMS);
		}
	}
}

int TFFmpegPlayer::GetMediaInfo(FFSettings *pSettings)
{
	if(_ctx->videoStream)
	{
		AVStream *pVS = _ctx->videoStream;
		AVCodecContext *vCodecCtx = pVS->codec;
		AVRational frameRate = av_stream_get_r_frame_rate(pVS);
		pSettings->v.width = vCodecCtx->width;
		pSettings->v.height = vCodecCtx->height;
		pSettings->v.fpsNum = frameRate.num;
		pSettings->v.fpsDen = frameRate.den;
		pSettings->v.timebaseNum = pVS->time_base.num;
		pSettings->v.timebaseDen = pVS->time_base.den;
		pSettings->v.totalFrames = pVS->nb_frames;
		pSettings->v.duration = pVS->duration;
		pSettings->v.valid = 1;
		strcpy_s(pSettings->v.codecName, vCodecCtx->codec->long_name);
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
	return FF_OK;
}

int TFFmpegPlayer::SetAudioOutputSetting(FFAudioSetting *setting)
{
	if(_audioDecoder)
		return _audioDecoder->SetOutputSetting(setting);
	return FF_ERR_GENERAL;
}

int TFFmpegPlayer::Init(const FFInitSetting *setting)
{
	int ret;
	ret = InitCtx(setting);
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

		FFVideoSetting videoSetting;
		videoSetting.width = _ctx->videoStream->codec->width;
		videoSetting.height = _ctx->videoStream->codec->height;
		videoSetting.pixFmt = setting->framePixFmt >= 0 ? FF_GetAVPixFmt(setting->framePixFmt) : _ctx->videoStream->codec->pix_fmt;
		ret = _videoDecoder->SetOutputSetting(&videoSetting);
	}

	if(ret >= 0 && _ctx->audioStream)
	{
		_audioDecoder = new TFFmpegAudioDecoder(_ctx, _pkter);
		ret = _audioDecoder->Init();

		FFAudioSetting audioSetting;
		audioSetting.channels = setting->channels > 0 ? setting->channels : _ctx->audioStream->codec->channels;
		audioSetting.channelLayout = setting->channelLayout > 0 ? setting->channelLayout : av_get_default_channel_layout(audioSetting.channels);
		audioSetting.freq = setting->sampleRate > 0 ? setting->sampleRate : _ctx->audioStream->codec->sample_rate;
		audioSetting.sampleFmt = setting->sampleFmt >= 0 ? FF_GetAVSampleFmt(setting->sampleFmt) : _ctx->audioStream->codec->sample_fmt;
		ret = _audioDecoder->SetOutputSetting(&audioSetting);
	}

	if(ret >= 0)
	{
		_useExternalClock = setting->useExternalClock;
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
	if(!_ctx->videoStream)
		return FF_NO_VIDEO_STREAM;
	//open video codec context
	AVCodec *pCodec = avcodec_find_decoder(_ctx->videoStream->codec->codec_id);
	return avcodec_open2(_ctx->videoStream->codec, pCodec, NULL);
}

int TFFmpegPlayer::InitCtx(const FFInitSetting *setting)
{
	int ret = 0;
	char szFile[1024] = {0};

	if(!setting)
	{
		TFFLog(TFF_LOG_LEVEL_ERROR, "Init setting is null.");
		ret = FF_ERR_NOPOINTER;
		return ret;
	}

	FreeCtx();
	_ctx = (FFContext *)av_mallocz(sizeof(FFContext));
	_ctx->vsIndex = -1;
	_ctx->asIndex = -1;

	WideCharToMultiByte(
		CP_UTF8,
		0,
		setting->fileName,
		-1,
		szFile,
		1024,
		NULL,
		NULL);

	TFFLog(TFF_LOG_LEVEL_INFO, "Open file %s", szFile);

	ret = avformat_open_input(&_ctx->fmtCtx,
		szFile, NULL, NULL);

	if(ret < 0)
	{
		TFFLog(TFF_LOG_LEVEL_ERROR, "avformat_open_input failed. %d", ret);
		ret = FF_ERR_CANNOT_OPEN_FILE;
		return ret;
	}

	ret = avformat_find_stream_info(_ctx->fmtCtx, NULL);

	if(ret < 0)
	{
		TFFLog(TFF_LOG_LEVEL_ERROR, "avformat_find_stream_info failed. %d", ret);
		ret = FF_ERR_CANNOT_FIND_STREAM_INFO;
		return ret;
	}

	av_dump_format(_ctx->fmtCtx, 0, szFile, 0);

	if(!setting->videoDisable)
	{
		_ctx->vsIndex = av_find_best_stream(_ctx->fmtCtx, AVMEDIA_TYPE_VIDEO, _ctx->vsIndex, -1,
			NULL, 0);

		if(_ctx->vsIndex < 0)
		{
			TFFLog(TFF_LOG_LEVEL_INFO, "Cannot find video stream.");
		}
		else
		{
			_ctx->videoStream = _ctx->fmtCtx->streams[_ctx->vsIndex];
			ret = OpenVideoCodec();
			if(ret < 0)
			{
				TFFLog(TFF_LOG_LEVEL_DEBUG, "Open video codec failed.");
				ret = FF_ERR_CANNOT_OPEN_VIDEO_CODEC;
				return ret;
			}
		} 
	}

	if(!setting->audioDisable)
	{
		_ctx->asIndex = av_find_best_stream(_ctx->fmtCtx, AVMEDIA_TYPE_AUDIO, _ctx->asIndex,
			_ctx->vsIndex, NULL, 0);

		if(_ctx->asIndex < 0)
			TFFLog(TFF_LOG_LEVEL_INFO, "Cannot find audio stream.");
		else
		{
			_ctx->audioStream = _ctx->fmtCtx->streams[_ctx->asIndex];
			ret = OpenAudioCodec();
			if(ret < 0)
			{
				TFFLog(TFF_LOG_LEVEL_DEBUG, "Open audio codec failed.");
				ret = FF_ERR_CANNOT_OPEN_AUDIO_CODEC;
				return ret;
			}
		}
	}

	TFFLog(TFF_LOG_LEVEL_DEBUG, "Init stream successfully.");
	TFFLog(TFF_LOG_LEVEL_DEBUG, "Video stream index: %d", _ctx->vsIndex);
	TFFLog(TFF_LOG_LEVEL_DEBUG, "Audio stream index: %d", _ctx->asIndex);
	if(_ctx->videoStream)
	{
		TFFLog(TFF_LOG_LEVEL_DEBUG, "Video information:");
		TFFLog(TFF_LOG_LEVEL_DEBUG, "Width: %d Height: %d", _ctx->videoStream->codec->width, _ctx->videoStream->codec->height);
		TFFLog(TFF_LOG_LEVEL_DEBUG, "Codec name: %s", _ctx->videoStream->codec->codec->long_name);
	}
	if(_ctx->audioStream)
	{
		int64_t channelLayout = av_get_default_channel_layout(_ctx->audioStream->codec->channels);
		char channelStr[MAX_PATH] = {0};
		av_get_channel_layout_string(channelStr, MAX_PATH, _ctx->audioStream->codec->channels, channelLayout);
		TFFLog(TFF_LOG_LEVEL_DEBUG, "Audio information:");
		TFFLog(TFF_LOG_LEVEL_DEBUG, "Sample rate: %d MHz", _ctx->audioStream->codec->sample_rate);
		TFFLog(TFF_LOG_LEVEL_DEBUG, "Stream time base %d/%d", _ctx->audioStream->time_base.num, _ctx->audioStream->time_base.den);
		TFFLog(TFF_LOG_LEVEL_DEBUG, "Channles: %d", _ctx->audioStream->codec->channels);
		TFFLog(TFF_LOG_LEVEL_DEBUG, "Channel layout: %s", channelStr);
		TFFLog(TFF_LOG_LEVEL_DEBUG, "Codec name :%s", _ctx->audioStream->codec->codec->long_name);
	}

	if(!_ctx->audioStream && !_ctx->videoStream)
		ret = FF_ERR_NO_STREAM_FOUND;

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
		if(_audioWait)
		{
			if(pts > 0)
			{
				long sleepMS = (long)(pts - _clock.GetTime());
				if(sleepMS > 0)
				{
					TFFLog(TFF_LOG_LEVEL_INFO, "Audio will start in %d ms.", sleepMS);
					TFF_Sleep(sleepMS);
				}
			}
			_audioWait = 0;
		}
		else
			_clock.Sync((long)pts);
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
	if(type == TFF_PLAYTYPE_VIDEO)
		TFFLog(TFF_LOG_LEVEL_DEBUG, "Video finished.");
	else if(type == TFF_PLAYTYPE_AUDIO)
		TFFLog(TFF_LOG_LEVEL_DEBUG, "Audio finished.");

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
		if(_ctx->fmtCtx)
			avformat_close_input(&_ctx->fmtCtx);
		av_freep(&_ctx);
	}
}