#include "TFFmpegPacketer.h"
#include "TFFmpegUtil.h"

#define FF_MAX_PACKET_COUNT 50
#define FF_MIN_PACKET_COUNT 5

//32MB
#define FF_MAX_PACKET_SIZE 0x02000000

TFFmpegPacketer::TFFmpegPacketer(FFContext *p)
	:_hThread(NULL),
	_videoQ(NULL),
	_audioQ(NULL),
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
	if(_hThread)
	{
		TFF_WaitThread(_hThread, 1000);
		CloseThreadP(&_hThread);
	}
	DestroyPktQueue(&_videoQ);
	DestroyPktQueue(&_audioQ);
	TFF_CloseMutex(_readMutex);
	TFF_DestroyCond(&_readCond);
}

int TFFmpegPacketer::Init()
{
	//create packet queue
	InitPacketQueue(&_videoQ, PKT_Q_VIDEO);
	InitPacketQueue(&_audioQ, PKT_Q_AUDIO);

	_readCond = TFF_CreateCond();
	_readMutex = TFF_CreateMutex();
	return 0;
}

int TFFmpegPacketer::InitPacketQueue(FFPacketQueue **ppq, int type)
{
	*ppq = (FFPacketQueue *)malloc(sizeof(FFPacketQueue));
	memset(*ppq, 0, sizeof(FFPacketQueue));
	(*ppq)->mutex = TFF_CreateMutex();
	(*ppq)->cond = TFF_CreateCond();
	(*ppq)->type = type;
	return 0;
}

int TFFmpegPacketer::Start()
{
	if(!_hThread)
		_hThread = TFF_CreateThread(SThreadStart, this);
	return 0;
}

int TFFmpegPacketer::SeekPos(int64_t pos)
{
	int err = 0;
	TFF_GetMutex(_readMutex, TFF_INFINITE);
	TFF_GetMutex(_videoQ->mutex, TFF_INFINITE);
	TFF_GetMutex(_audioQ->mutex, TFF_INFINITE);

	ClearPktQueue(_videoQ);
	if(_ctx->vsIndex > 0)
	{
		err = av_seek_frame(_ctx->pFmtCtx, _ctx->vsIndex, pos, AVSEEK_FLAG_BACKWARD);
		if(err < 0)
			DebugOutput("TFFmpegPacketer::SeekPos video ret %d", err);
	}

	ClearPktQueue(_audioQ);
	if(_ctx->asIndex > 0)
	{
		err = av_seek_frame(_ctx->pFmtCtx, _ctx->asIndex, pos, AVSEEK_FLAG_BACKWARD);
		if(err < 0)
			DebugOutput("TFFmpegPacketer::SeekPos audio ret %d", err);
	}

	_isFinished = FALSE;

	TFF_CondBroadcast(_readCond);
	TFF_ReleaseMutex(_audioQ->mutex);
	TFF_ReleaseMutex(_videoQ->mutex);
	TFF_ReleaseMutex(_readMutex);
	return 0;
}

int TFFmpegPacketer::GetVideoPacketCount()
{
	return GetPacketCount(_videoQ);
}

int TFFmpegPacketer::GetPacketCount(FFPacketQueue *q)
{
	TFF_GetMutex(q->mutex, TFF_INFINITE);
	int count = q->count;
	TFF_ReleaseMutex(q->mutex);
	return count;
}

unsigned long __stdcall TFFmpegPacketer::SThreadStart(void *p)
{
	TFFmpegPacketer *pThis = (TFFmpegPacketer *)p;
	pThis->ThreadStart();
	return 0;
}

BOOL TFFmpegPacketer::IsFinished()
{
	return _isFinished;
}

void __stdcall TFFmpegPacketer::ThreadStart()
{
	DebugOutput("TFFmpegPacketer::Thread begin.");
	int readRet = 0;
	while(1)
	{
		if(_cmd & PkterCmd_Exit)
			break;

		TFF_GetMutex(_readMutex, TFF_INFINITE);
		while(_videoQ->size + _audioQ->size >= FF_MAX_PACKET_SIZE
			&& !(_cmd & PkterCmd_Exit))
		{
			TFF_WaitCond(_readCond, _readMutex);
		}
		AVPacket *pPkt = (AVPacket *)av_mallocz(sizeof(AVPacket));
		av_init_packet(pPkt);
		readRet = av_read_frame(_ctx->pFmtCtx, pPkt);
		if(readRet >= 0)
		{
			DebugOutput("index:%d dts:%d pts:%d time:%f",
				pPkt->stream_index,
				(int)pPkt->dts,
				(int)pPkt->pts,
				(double)(pPkt->dts * av_q2d(_ctx->pFmtCtx->streams[pPkt->stream_index]->time_base)));
			if(_ctx->handleVideo && pPkt->stream_index == _ctx->vsIndex)
				PutIntoPktQueue(_videoQ, pPkt);
			else if(_ctx->handleAudio && pPkt->stream_index == _ctx->asIndex)
				PutIntoPktQueue(_audioQ, pPkt);
			else
			{
				//DebugOutput("TFFmpegPacketer::Thread drop packet.\n");
				av_free_packet(pPkt);
				av_free(pPkt);
			}
		}
		else/* if(readRet == AVERROR_EOF)*/
		{
			av_free(pPkt);
			DebugOutput("TFFmpegPacketer::Thread to end of file.\n");
			_isFinished = TRUE;
			TFF_CondBroadcast(_videoQ->cond);
			TFF_CondBroadcast(_audioQ->cond);
			TFF_WaitCond(_readCond, _readMutex);
			DebugOutput("TFFmpegPacketer::Thread awake.\n");
		}
		TFF_ReleaseMutex(_readMutex);
	}
	DebugOutput("TFFmpegPacketer::Thread exit.");
}

int TFFmpegPacketer::GetVideoPacket(FFPacketList **ppPktList)
{
	return GetPacket(_videoQ, ppPktList);
}

int TFFmpegPacketer::GetAudioPacket(FFPacketList **ppPkt)
{
	return GetPacket(_audioQ, ppPkt);
}

int TFFmpegPacketer::GetPacket(FFPacketQueue *q, FFPacketList **ppPkt)
{
	int ret = 0;
	TFF_GetMutex(q->mutex, TFF_INFINITE);

	while(q->count == 0 &&
		!_isFinished)
	{
		DebugOutput("Count of packet queue is 0 and not finished. Will wait for cond. Queue type %d", q->type);
		TFF_WaitCond(q->cond, q->mutex);
		DebugOutput("Got signal. Queue type %d", q->type);
	}

	if(q->count == 0 && _isFinished)
	{
		*ppPkt = NULL;
		ret = -1;
	}
	else
	{
		FFPacketList *first = q->first;
		q->first = q->first->next;
		q->count--;
		q->size -= first->pPkt->size;
		*ppPkt = first;
		if(q->count <= 1)
			TFF_CondBroadcast(_readCond);
	}
	TFF_ReleaseMutex(q->mutex);
	return ret;
}

int TFFmpegPacketer::PutIntoPktQueue(
		FFPacketQueue *q,
		AVPacket *pPkt)
{
	TFF_GetMutex(q->mutex, TFF_INFINITE);
	FFPacketList *pNew = (FFPacketList *)av_mallocz(sizeof(FFPacketList));
	pNew->pPkt = pPkt;
	if(q->count == 0)
		q->first = q->last = pNew;
	else
	{
		q->last->next = pNew;
		q->last = pNew;
	}
	q->count++;
	q->size += pPkt->size;
	if(q->count == 1)
		TFF_CondBroadcast(q->cond);
	TFF_ReleaseMutex(q->mutex);
	return 0;
}

int TFFmpegPacketer::DestroyPktQueue(FFPacketQueue **ppq)
{
	ClearPktQueue(*ppq);
	TFF_CloseMutex((*ppq)->mutex);
	TFF_DestroyCond(&(*ppq)->cond);
	free(*ppq);
	*ppq = NULL;
	return 0;
}

int TFFmpegPacketer::ClearPktQueue(FFPacketQueue *q)
{
	FFPacketList *pCur = q->first;
	FFPacketList *pNext = NULL;
	while(pCur != NULL)
	{
		pNext = pCur->next;
		FreeSinglePktList(&pCur);
		pCur = pNext;
	}
	q->first = q->last = NULL;
	q->count = 0;
	q->size = 0;
	return 0;
}

int TFFmpegPacketer::FreeSinglePktList(FFPacketList **ppPktList)
{
	FFPacketList *pPktList = *ppPktList;
	if(pPktList->pPkt != NULL)
	{
		av_free_packet(pPktList->pPkt);
		av_free(pPktList->pPkt);
	}
	av_free(pPktList);
	*ppPktList = NULL;
	return 0;
}