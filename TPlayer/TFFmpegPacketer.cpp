#include "TFFmpegPacketer.h"
#include "TFFmpegUtil.h"

#define FF_MAX_PACKET_COUNT 50

#define FF_MAX_PACKET_SIZE 0x00300000

TFFmpegPacketer::TFFmpegPacketer(FFContext *p)
	:_hThread(NULL),
	_pVQ(NULL),
	_pAQ(NULL),
	_cmd(PkterCmd_None),
	_isFinished(FALSE),
	_hEOFEvent(NULL),
	_avReadMutex(NULL)
{
	_pCtx = p;
}

TFFmpegPacketer::~TFFmpegPacketer()
{
	_cmd |= PkterCmd_Exit;
	TFF_SetEvent(_hEOFEvent);
	TFF_CondBroadcast(_pVQ->cond);
	TFF_CondBroadcast(_pAQ->cond);
	if(_hThread)
	{
		TFF_WaitThread(_hThread, 1000);
		CloseThreadP(&_hThread);
	}
	DestroyPktQueue(&_pVQ);
	DestroyPktQueue(&_pAQ);
	CloseEventP(&_hEOFEvent);
	CloseMutexP(&_avReadMutex);
}

int TFFmpegPacketer::Init()
{
	//create eof event
	_hEOFEvent = TFF_CreateEvent(FALSE, FALSE);

	//create packet queue
	InitPacketQueue(&_pVQ, PKT_Q_VIDEO);
	InitPacketQueue(&_pAQ, PKT_Q_AUDIO);

	//create av read mutex
	_avReadMutex = TFF_CreateMutex();
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
	TFF_GetMutex(_avReadMutex, TFF_INFINITE);
	TFF_GetMutex(_pVQ->mutex, TFF_INFINITE);

	ClearPktQueue(_pVQ);
	if(_pCtx->vsIndex > 0)
	{
		err = av_seek_frame(_pCtx->pFmtCtx,
			_pCtx->vsIndex,
			pos,
			AVSEEK_FLAG_BACKWARD);
	}
	if(err < 0)
		DebugOutput("TFFmpegPacketer::SeekPos video ret %d", err);

	TFF_GetMutex(_pAQ->mutex, TFF_INFINITE);
	
	ClearPktQueue(_pAQ);
	if(_pCtx->asIndex > 0)
	{
		err = av_seek_frame(_pCtx->pFmtCtx,
			_pCtx->asIndex,
			pos,
			AVSEEK_FLAG_BACKWARD);
	}
	if(err < 0)
		DebugOutput("TFFmpegPacketer::SeekPos video ret %d", err);

	_cmd |= PkterCmd_Abandon;
	_isFinished = FALSE;

	TFF_SetEvent(_hEOFEvent);
	TFF_CondBroadcast(_pVQ->cond);
	TFF_CondBroadcast(_pAQ->cond);
	TFF_ReleaseMutex(_pAQ->mutex);
	TFF_ReleaseMutex(_pVQ->mutex);
	TFF_ReleaseMutex(_avReadMutex);
	return 0;
}

int TFFmpegPacketer::GetVideoPacketCount()
{
	return GetPacketCount(_pVQ);
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
	while(true)
	{
		if(_cmd & PkterCmd_Exit)
			break;

		AVPacket *pPkt = (AVPacket *)av_mallocz(sizeof(AVPacket));
		av_init_packet(pPkt);
		//DebugOutput("TFFmpegPacketer::Thread Before av_read_frame\n");
		TFF_GetMutex(_avReadMutex, TFF_INFINITE);
		readRet = av_read_frame(_pCtx->pFmtCtx, pPkt);
		_cmd &= ~PkterCmd_Abandon;
		TFF_ReleaseMutex(_avReadMutex);
		if(readRet >= 0)
		{
			if(_pCtx->handleVideo && pPkt->stream_index == _pCtx->vsIndex)
				PutIntoPktQueue(_pVQ, pPkt);
			else if(_pCtx->handleAudio && pPkt->stream_index == _pCtx->asIndex)
				PutIntoPktQueue(_pAQ, pPkt);
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
			TFF_CondBroadcast(_pVQ->cond);
			TFF_CondBroadcast(_pAQ->cond);
			TFF_WaitEvent(_hEOFEvent, TFF_INFINITE);
			DebugOutput("TFFmpegPacketer::Thread awake from hPktThreadWaitEvent.\n");
		}
	}
	DebugOutput("TFFmpegPacketer::Thread exit.");
}

int TFFmpegPacketer::GetVideoPacket(FFPacketList **ppPktList)
{
	return GetPacket(_pVQ, ppPktList);
}

int TFFmpegPacketer::GetAudioPacket(FFPacketList **ppPkt)
{
	return GetPacket(_pAQ, ppPkt);
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

	if(_isFinished)
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
		if(q->count < (FF_MAX_PACKET_COUNT / 2))
			TFF_CondBroadcast(q->cond);
	}
	TFF_ReleaseMutex(q->mutex);
	return ret;
}

int TFFmpegPacketer::PutIntoPktQueue(
		FFPacketQueue *q,
		AVPacket *pPkt)
{
	FFPacketList *pNew = NULL;
	TFF_GetMutex(q->mutex, TFF_INFINITE);
	while(q->count >= FF_MAX_PACKET_COUNT &&
		!(_cmd & PkterCmd_Abandon) &&
		!(_cmd & PkterCmd_Exit))
		TFF_WaitCond(q->cond, q->mutex);

	if((_cmd & PkterCmd_Abandon) || 
		(_cmd & PkterCmd_Exit))
	{
		DebugOutput("Packet abandon.");
		av_free_packet(pPkt);
		av_free(pPkt);
	}
	else
	{
		pNew = (FFPacketList *)av_mallocz(sizeof(FFPacketList));
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
	}
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
		av_free_packet((AVPacket *)pPktList->pPkt);
		av_free(pPktList->pPkt);
	}
	av_free(pPktList);
	*ppPktList = NULL;
	return 0;
}