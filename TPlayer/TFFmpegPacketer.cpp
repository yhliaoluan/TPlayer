#include "TFFmpegPacketer.h"
#include "TFFmpegUtil.h"

#define FF_MAX_PACKET_COUNT 10

TFFmpegPacketer::TFFmpegPacketer(FFContext *p)
	:_hThread(NULL),
	_pQ(NULL),
	_cmd(PkterCmd_None),
	_isFinished(FALSE)
{
	_pCtx = p;
}

TFFmpegPacketer::~TFFmpegPacketer()
{
	_cmd |= PkterCmd_Exit;
	TFF_SetEvent(_hEOFEvent);
	if(_hThread)
	{
		TFF_WaitThread(_hThread, 1000);
		CloseThreadP(&_hThread);
	}
	DestroyPktQueue();
	CloseEventP(&_hEOFEvent);
}

int TFFmpegPacketer::Init()
{
	_hEOFEvent = TFF_CreateEvent(FALSE, FALSE);
	_pQ = (FFPacketQueue *)malloc(sizeof(FFPacketQueue));
	memset(_pQ, 0, sizeof(FFPacketQueue));
	_pQ->mutex = TFF_CreateMutex();
	_pQ->cond = TFF_CreateCond();
	_pQ->first = _pQ->last = NULL;
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
	TFF_GetMutex(_pQ->mutex, INFINITE);
	ClearPktQueue();
	av_seek_frame(_pCtx->pFmtCtx,
		_pCtx->videoStreamIdx,
		pos,
		AVSEEK_FLAG_BACKWARD);
	TFF_CondBroadcast(_pQ->cond);
	TFF_SetEvent(_hEOFEvent);
	TFF_ReleaseMutex(_pQ->mutex);
	return 0;
}

int TFFmpegPacketer::GetPacketCount()
{
	TFF_GetMutex(_pQ->mutex, TFF_INFINITE);
	int count = 0;
	if(_pQ != NULL)
		count = _pQ->count;
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

		TFF_GetMutex(_pQ->mutex, INFINITE);
		//if the list is full
		//thread will wait
		while(_pQ->count >= FF_MAX_PACKET_COUNT)
			TFF_WaitCond(_pQ->cond, _pQ->mutex);

		AVPacket *pPkt = (AVPacket *)av_mallocz(sizeof(AVPacket));
		av_init_packet(pPkt);
		//DebugOutput("TFFmpegPacketer::Thread Before av_read_frame\n");
		readRet = av_read_frame(_pCtx->pFmtCtx, pPkt);
		if(readRet >= 0)
		{
			if(pPkt->stream_index != _pCtx->videoStreamIdx)
			{
				//DebugOutput("TFFmpegPacketer::Thread drop packet.\n");
				TFF_ReleaseMutex(_pQ->mutex);
				av_free_packet(pPkt);
				av_free(pPkt);
			}
			else
			{
				PutIntoPktQueue(PktOpe_None, pPkt);
				TFF_ReleaseMutex(_pQ->mutex);
			}
		}
		else/* if(readRet == AVERROR_EOF)*/
		{
			TFF_ReleaseMutex(_pQ->mutex);
			av_free(pPkt);
			DebugOutput("TFFmpegPacketer::Thread to end of file.\n");
			_isFinished = TRUE;
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

	if(_pQ->count == 0)
	{
		*ppPktList = NULL;
		ret = -1;
	}
	else
	{
		*ppPktList = _pQ->first;
		_pQ->first = _pQ->first->next;
		_pQ->count--;
	}
	TFF_ReleaseMutex(_pQ->mutex);

	//Notify the thread the list is not full.
	if(ret >= 0)
		TFF_CondBroadcast(_pQ->cond);
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