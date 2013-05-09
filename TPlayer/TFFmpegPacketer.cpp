#include "TFFmpegPacketer.h"
#include "TFFmpegUtil.h"

#define FF_MAX_PACKET_COUNT 10

//1MB
#define FF_MAX_PACKET_SIZE 1048576

TFFmpegPacketer::TFFmpegPacketer(FFContext *p)
	:_hThread(NULL),
	_pQ(NULL),
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
	TFF_CondBroadcast(_pQ->cond);
	if(_hThread)
	{
		TFF_WaitThread(_hThread, 1000);
		CloseThreadP(&_hThread);
	}
	DestroyPktQueue();
	CloseEventP(&_hEOFEvent);
	CloseMutexP(&_avReadMutex);
}

int TFFmpegPacketer::Init()
{
	//create eof event
	_hEOFEvent = TFF_CreateEvent(FALSE, FALSE);

	//create packet queue
	_pQ = (FFPacketQueue *)malloc(sizeof(FFPacketQueue));
	memset(_pQ, 0, sizeof(FFPacketQueue));
	_pQ->mutex = TFF_CreateMutex();
	_pQ->cond = TFF_CreateCond();

	//create av read mutex
	_avReadMutex = TFF_CreateMutex();
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
	TFF_GetMutex(_avReadMutex, TFF_INFINITE);
	TFF_GetMutex(_pQ->mutex, TFF_INFINITE);
	ClearPktQueue();
	av_seek_frame(_pCtx->pFmtCtx,
		_pCtx->videoStreamIdx,
		pos,
		AVSEEK_FLAG_BACKWARD);
	_cmd |= PkterCmd_Abandon;
	TFF_CondBroadcast(_pQ->cond);
	TFF_SetEvent(_hEOFEvent);
	TFF_ReleaseMutex(_pQ->mutex);
	TFF_ReleaseMutex(_avReadMutex);
	return 0;
}

int TFFmpegPacketer::GetPacketCount()
{
	TFF_GetMutex(_pQ->mutex, TFF_INFINITE);
	int count = _pQ->count;
	TFF_ReleaseMutex(_pQ->mutex);
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
			if(pPkt->stream_index != _pCtx->videoStreamIdx)
			{
				//DebugOutput("TFFmpegPacketer::Thread drop packet.\n");
				av_free_packet(pPkt);
				av_free(pPkt);
			}
			else
			{
				//DebugOutput("TFFmpegPacketer::Thread will get mutex.\n");
				TFF_GetMutex(_pQ->mutex, TFF_INFINITE);

				//if the list is full
				//and there is no abandon command
				//thread will wait
				while(_pQ->size >= FF_MAX_PACKET_SIZE &&
					!(_cmd & PkterCmd_Abandon) &&
					!(_cmd & PkterCmd_Exit))
					TFF_WaitCond(_pQ->cond, _pQ->mutex);

				if((_cmd & PkterCmd_Abandon) || 
					(_cmd & PkterCmd_Exit))
				{
					DebugOutput("TFFmpegPacketer::Thread abandon package.\n");
					av_free_packet(pPkt);
					av_free(pPkt);
				}
				else
				{
					PutIntoPktQueue(PktOpe_None, pPkt);
					TFF_CondBroadcast(_pQ->cond);
					//DebugOutput("TFFmpegPacketer::Thread after put into queue. count:%d size:%d", _pQ->count, _pQ->size);
				}
				TFF_ReleaseMutex(_pQ->mutex);
				//DebugOutput("TFFmpegPacketer::Thread release mutex.\n");
			}
		}
		else/* if(readRet == AVERROR_EOF)*/
		{
			av_free(pPkt);
			DebugOutput("TFFmpegPacketer::Thread to end of file.\n");
			_isFinished = TRUE;
			TFF_CondBroadcast(_pQ->cond);
			TFF_WaitEvent(_hEOFEvent, TFF_INFINITE);
			_isFinished = FALSE;
			DebugOutput("TFFmpegPacketer::Thread awake from hPktThreadWaitEvent.\n");
		}
	}
	DebugOutput("TFFmpegPacketer::Thread exit.");
}

int TFFmpegPacketer::GetFirstPktOpe(enum FFPktOpe * pOpe)
{
	int ret = 0;
	TFF_GetMutex(_pQ->mutex, TFF_INFINITE);
	if(_pQ->count == 0)
	{
		ret = -1;
		*pOpe = PktOpe_None;
	}
	else
		*pOpe = _pQ->first->ope;
	TFF_ReleaseMutex(_pQ->mutex);
	return ret;
}

int TFFmpegPacketer::GetPacket(FFPacketList **ppPktList)
{
	int ret = 0;
	TFF_GetMutex(_pQ->mutex, TFF_INFINITE);

	while(_pQ->count == 0 && 
		!_isFinished)
		TFF_WaitCond(_pQ->cond, _pQ->mutex);

	if(_isFinished)
	{
		*ppPktList = NULL;
		ret = -1;
	}
	else
	{
		FFPacketList *first = _pQ->first;
		_pQ->first = _pQ->first->next;
		_pQ->count--;
		if(first->ope == PktOpe_None)
			_pQ->size -= ((AVPacket *)first->pPkt)->size;
		*ppPktList = first;

		//Notify that list is not full.
		TFF_CondBroadcast(_pQ->cond);
	}
	TFF_ReleaseMutex(_pQ->mutex);
	//Notify the thread the list is not full.
	return ret;
}

int TFFmpegPacketer::PutIntoPktQueue(
		enum FFPktOpe opeType,
		void *pPkt)
{
	FFPacketList *pNew = (FFPacketList *)av_mallocz(sizeof(FFPacketList));
	pNew->ope = opeType;
	pNew->pPkt = pPkt;

	if(_pQ->count == 0)
		_pQ->first = _pQ->last = pNew;
	else
	{
		_pQ->last->next = pNew;
		_pQ->last = pNew;
	}

	_pQ->count++;
	if(opeType == PktOpe_None)
		_pQ->size += ((AVPacket *)pPkt)->size;
	return 0;
}

int TFFmpegPacketer::DestroyPktQueue()
{
	ClearPktQueue();
	TFF_DestroyCond(&_pQ->cond);
	TFF_CloseMutex(_pQ->mutex);
	free(_pQ);
	_pQ = NULL;
	return 0;
}

int TFFmpegPacketer::ClearPktQueue()
{
	FFPacketList *pCur = _pQ->first;
	FFPacketList *pNext = NULL;
	while(pCur != NULL)
	{
		pNext = pCur->next;
		FreeSinglePktList(&pCur);
		pCur = pNext;
	}
	_pQ->first = _pQ->last = NULL;
	_pQ->count = 0;
	_pQ->size = 0;
	return 0;
}

int TFFmpegPacketer::FreeSinglePktList(FFPacketList **ppPktList)
{
	FFPacketList *pPktList = *ppPktList;
	if(pPktList->pPkt != NULL)
	{
		if(pPktList->ope == PktOpe_None)
			av_free_packet((AVPacket *)pPktList->pPkt);
		av_free(pPktList->pPkt);
	}
	av_free(pPktList);
	*ppPktList = NULL;
	return 0;
}