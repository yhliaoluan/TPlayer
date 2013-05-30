#include "TFFmpegPacketer.h"
#include "TFFmpegUtil.h"

//16MB
#define FF_MAX_PACKET_SIZE 0x01000000

TFFmpegPacketer::TFFmpegPacketer(FF_CONTEXT *p)
	:_thread(NULL),
	_videoQ(NULL),
	_audioQ(NULL),
	_subtitleQ(NULL),
	_cmd(PkterCmd_None),
	_isFinished(FALSE),
	_readMutex(NULL),
	_readCond(NULL)
{
	_ctx = p;
}

TFFmpegPacketer::~TFFmpegPacketer()
{
	_cmd |= PkterCmd_Exit;
	TFF_CondBroadcast(_readCond);
	if (_thread)
	{
		TFF_WaitThread(_thread, 1000);
		CloseThreadP(&_thread);
	}
	DestroyPktQueue(&_videoQ);
	DestroyPktQueue(&_audioQ);
	DestroyPktQueue(&_subtitleQ);
	TFF_CloseMutex(_readMutex);
	TFF_CondDestroy(&_readCond);
}

int TFFmpegPacketer::Init()
{
	//create packet queue
	InitPacketQueue(&_videoQ, PKT_Q_VIDEO);
	InitPacketQueue(&_audioQ, PKT_Q_AUDIO);
	InitPacketQueue(&_subtitleQ, PKT_Q_SUBTITLE);

	_readCond = TFF_CondCreate();
	_readMutex = TFF_CreateMutex();
	return 0;
}

int TFFmpegPacketer::InitPacketQueue(FF_PACKET_QUEUE **ppq, int type)
{
	*ppq = (FF_PACKET_QUEUE *)malloc(sizeof(FF_PACKET_QUEUE));
	memset(*ppq, 0, sizeof(FF_PACKET_QUEUE));
	(*ppq)->mutex = TFF_CreateMutex();
	(*ppq)->cond = TFF_CondCreate();
	(*ppq)->type = type;
	return 0;
}

int TFFmpegPacketer::Start()
{
	if (!_thread)
		_thread = TFF_CreateThread(SThreadStart, this);
	return 0;
}

int TFFmpegPacketer::SeekPos(double time)
{
	int err = 0;
	int64_t pos, vpos, apos;
	AVRational timeBaseQ = {1, AV_TIME_BASE};
	TFF_GetMutex(_readMutex, TFF_INFINITE);
	TFF_GetMutex(_videoQ->mutex, TFF_INFINITE);
	TFF_GetMutex(_audioQ->mutex, TFF_INFINITE);

	pos = (int64_t)(time * AV_TIME_BASE);
	
	ClearPktQueue(_videoQ);
	if (_ctx->vsIndex > 0)
	{
		vpos = av_rescale_q(pos, timeBaseQ, _ctx->videoStream->time_base); 
		err = av_seek_frame(_ctx->fmtCtx, _ctx->vsIndex, vpos, AVSEEK_FLAG_BACKWARD);
		if (err < 0)
			TFFLog(TFF_LOG_LEVEL_DEBUG, "TFFmpegPacketer::SeekPos video ret %d", err);
	}

	ClearPktQueue(_audioQ);
	if (_ctx->asIndex > 0)
	{
		apos = av_rescale_q(pos, timeBaseQ, _ctx->audioStream->time_base);
		err = av_seek_frame(_ctx->fmtCtx, _ctx->asIndex, apos, AVSEEK_FLAG_BACKWARD);
		if (err < 0)
			TFFLog(TFF_LOG_LEVEL_DEBUG, "TFFmpegPacketer::SeekPos audio ret %d", err);
	}

	_isFinished = FALSE;

	TFF_CondBroadcast(_readCond);
	TFF_ReleaseMutex(_audioQ->mutex);
	TFF_ReleaseMutex(_videoQ->mutex);
	TFF_ReleaseMutex(_readMutex);
	return 0;
}

unsigned long __stdcall TFFmpegPacketer::SThreadStart(void *p)
{
	TFFmpegPacketer *t = (TFFmpegPacketer *)p;
	t->ThreadStart();
	return 0;
}

void __stdcall TFFmpegPacketer::ThreadStart()
{
	TFFLog(TFF_LOG_LEVEL_DEBUG, "TFFmpegPacketer::Thread begin.");
	int readRet = 0;
	while (1)
	{
		TFF_GetMutex(_readMutex, TFF_INFINITE);
		while (_videoQ->size + _audioQ->size >= FF_MAX_PACKET_SIZE
			&& !(_cmd & PkterCmd_Exit))
		{
			TFF_CondWait(_readCond, _readMutex);
		}

		if (_cmd & PkterCmd_Exit)
			break;

		AVPacket *pkt = (AVPacket *)av_mallocz(sizeof(AVPacket));
		av_init_packet(pkt);
		readRet = av_read_frame(_ctx->fmtCtx, pkt);
		if (readRet >= 0)
		{
			if (pkt->stream_index == _ctx->vsIndex)
				PutIntoPktQueue(_videoQ, pkt);
			else if (pkt->stream_index == _ctx->asIndex)
				PutIntoPktQueue(_audioQ, pkt);
			//TODO: handle subtitle stream
			/*else if (pkt->stream_index == _ctx->ssIndex)
				PutIntoPktQueue(_subtitleQ, pkt);*/
			else
			{
				//TFFLog(TFF_LOG_LEVEL_DEBUG, "TFFmpegPacketer::Thread drop packet.\n");
				av_free_packet(pkt);
				av_free(pkt);
			}
		}
		else/* if (readRet == AVERROR_EOF)*/
		{
			av_free(pkt);
			TFFLog(TFF_LOG_LEVEL_DEBUG, "TFFmpegPacketer::Thread to end of file.");
			_isFinished = TRUE;
			TFF_CondBroadcast(_videoQ->cond);
			TFF_CondBroadcast(_audioQ->cond);
			TFF_CondBroadcast(_subtitleQ->cond);
			TFF_CondWait(_readCond, _readMutex);
			TFFLog(TFF_LOG_LEVEL_DEBUG, "TFFmpegPacketer::Thread awake.");
		}
		TFF_ReleaseMutex(_readMutex);
	}
	TFFLog(TFF_LOG_LEVEL_DEBUG, "TFFmpegPacketer::Thread exit.");
}

int TFFmpegPacketer::GetVideoPacket(FF_PACKET_LIST **pkt)
{
	return GetPacket(_videoQ, pkt);
}

int TFFmpegPacketer::GetAudioPacket(FF_PACKET_LIST **pkt)
{
	return GetPacket(_audioQ, pkt);
}

int TFFmpegPacketer::GetSubtitlePacket(FF_PACKET_LIST **pkt)
{
	return GetPacket(_subtitleQ, pkt);
}

int TFFmpegPacketer::GetPacket(FF_PACKET_QUEUE *q, FF_PACKET_LIST **ppkt)
{
	int ret = 0;
	TFF_GetMutex(q->mutex, TFF_INFINITE);

	while (q->count == 0 &&
		!_isFinished)
	{
		TFFLog(TFF_LOG_LEVEL_DEBUG, "Count of packet queue is 0 and not finished. Will wait for cond. Queue type %d", q->type);
		TFF_CondWait(q->cond, q->mutex);
		TFFLog(TFF_LOG_LEVEL_DEBUG, "Got signal. Queue type %d", q->type);
	}

	if (q->count == 0 && _isFinished)
	{
		*ppkt = NULL;
		ret = -1;
	}
	else
	{
		FF_PACKET_LIST *first = q->first;
		q->first = q->first->next;
		q->count--;
		q->size -= first->pkt->size;
		*ppkt = first;
		if (q->count <= 1 && !_isFinished)
			TFF_CondBroadcast(_readCond);
	}
	TFF_ReleaseMutex(q->mutex);
	return ret;
}

int TFFmpegPacketer::PutIntoPktQueue(
		FF_PACKET_QUEUE *q,
		AVPacket *pkt)
{
	TFF_GetMutex(q->mutex, TFF_INFINITE);
	FF_PACKET_LIST *pNew = (FF_PACKET_LIST *)av_mallocz(sizeof(FF_PACKET_LIST));
	pNew->pkt = pkt;
	if (q->count == 0)
		q->first = q->last = pNew;
	else
	{
		q->last->next = pNew;
		q->last = pNew;
	}
	q->count++;
	q->size += pkt->size;
	if (q->count == 1)
		TFF_CondBroadcast(q->cond);
	TFF_ReleaseMutex(q->mutex);
	return 0;
}

int TFFmpegPacketer::DestroyPktQueue(FF_PACKET_QUEUE **ppq)
{
	ClearPktQueue(*ppq);
	TFF_CloseMutex((*ppq)->mutex);
	TFF_CondDestroy(&(*ppq)->cond);
	free(*ppq);
	*ppq = NULL;
	return 0;
}

int TFFmpegPacketer::ClearPktQueue(FF_PACKET_QUEUE *q)
{
	FF_PACKET_LIST *cur = q->first;
	FF_PACKET_LIST *next = NULL;
	while (cur != NULL)
	{
		next = cur->next;
		FreeSinglePktList(&cur);
		cur = next;
	}
	q->first = q->last = NULL;
	q->count = 0;
	q->size = 0;
	return 0;
}

int TFFmpegPacketer::FreeSinglePktList(FF_PACKET_LIST **ppktList)
{
	FF_PACKET_LIST *pkt = *ppktList;
	if (pkt->pkt != NULL)
	{
		av_free_packet(pkt->pkt);
		av_free(pkt->pkt);
	}
	av_free(pkt);
	*ppktList = NULL;
	return 0;
}