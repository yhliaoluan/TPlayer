#include "TFFmpegPacketer.h"
#include "TFFmpegUtil.h"

#define FF_MAX_PACKET_COUNT 10

TFFmpegPacketer::TFFmpegPacketer(FFContext *p)
	:_hThread(NULL),
	_pPktList(NULL),
	_cmd(PkterCmd_None),
	_isFinished(FALSE)
{
	_pCtx = p;
}

TFFmpegPacketer::~TFFmpegPacketer()
{
	_cmd |= PkterCmd_Exit;
	TFF_SetEvent(_hGetPktEvent);
	TFF_SetEvent(_hEOFEvent);
	if(_hThread)
	{
		TFF_WaitThread(_hThread, 1000);
		CloseThreadP(&_hThread);
	}
	FreePktList();
	CloseMutexP(&_hPktListMutex);
	CloseEventP(&_hGetPktEvent);
	CloseEventP(&_hEOFEvent);
	CloseEventP(&_hSeekEvent);
}

int TFFmpegPacketer::Init()
{
	_hPktListMutex = TFF_CreateMutex();
	_hGetPktEvent = TFF_CreateEvent();
	_hEOFEvent = TFF_CreateEvent();
	_hSeekEvent = TFF_CreateEvent();
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
	_pos = pos;
	_cmd |= PkterCmd_Seek;
	TFF_SetEvent(_hGetPktEvent);
	TFF_SetEvent(_hEOFEvent);
	TFF_WaitEvent(_hSeekEvent, TFF_INFINITE);
	DebugOutput("TFFmpegPacketer::SeekPos exit");
	return 0;
}

int TFFmpegPacketer::GetPacketCount()
{
	TFF_GetMutex(_hPktListMutex, TFF_INFINITE);
	int count = 0;
	if(_pPktList != NULL)
		count = _pPktList->count;
	TFF_ReleaseMutex(_hPktListMutex);

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
	while(true)
	{
		if(_cmd & PkterCmd_Exit)
			break;

		if(_cmd & PkterCmd_Seek)
		{
			DebugOutput("TFFmpegPacketer::Thread Begin seek position.");
			FreePktList();
			av_seek_frame(_pCtx->pFmtCtx,
				_pCtx->videoStreamIdx,
				_pos,
				AVSEEK_FLAG_BACKWARD);
			FFSeekPosPkt *pSeekPkt = (FFSeekPosPkt *)av_mallocz(sizeof(FFSeekPosPkt));
			pSeekPkt->pos = _pos;
			PutIntoPktList(PktOpe_Flush, pSeekPkt);
			_cmd &= ~PkterCmd_Seek;
			TFF_SetEvent(_hSeekEvent);
			continue;
		}

		if(GetPacketCount() >= FF_MAX_PACKET_COUNT)
		{
			//DebugOutput("TFFmpegPacketer::Thread Packet list full. Will wait.");
			TFF_WaitEvent(_hGetPktEvent, TFF_INFINITE);
			continue;
		}

		AVPacket *pPkt = (AVPacket *)av_mallocz(sizeof(AVPacket));
		av_init_packet(pPkt);
		//DebugOutput("TFFmpegPacketer::Thread Before av_read_frame\n");
		if(av_read_frame(_pCtx->pFmtCtx, pPkt) >= 0)
		{
			if(pPkt->stream_index != _pCtx->videoStreamIdx)
			{
				//DebugOutput("TFFmpegPacketer::Thread drop packet.\n");
				av_free_packet(pPkt);
				av_free(pPkt);
			}
			else
			{
				//DebugOutput("TFFmpegPacketer::Thread add packet to packet list.\n");
				PutIntoPktList(PktOpe_None, pPkt);
			}
		}
		else
		{
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
	TFF_GetMutex(_hPktListMutex, TFF_INFINITE);
	if(_pPktList == NULL)
		ret = -1;
	else
		*pOpe = _pPktList->ope;
	TFF_ReleaseMutex(_hPktListMutex);
	return ret;
}

int TFFmpegPacketer::GetPacket(FFPacketList **ppPktList)
{
	int ret = 0;
	TFF_GetMutex(_hPktListMutex, TFF_INFINITE);

	if(_pPktList == NULL)
	{
		*ppPktList = NULL;
		ret = -1;
	}
	else
	{
		*ppPktList = _pPktList;
		_pPktList = _pPktList->next;
		if((*ppPktList)->count > 1)
			_pPktList->count = (*ppPktList)->count - 1;
	}

	TFF_ReleaseMutex(_hPktListMutex);

	//Notify the thread the list is not full.
	if(ret >= 0)
		TFF_SetEvent(_hGetPktEvent);
	return ret;
}

int TFFmpegPacketer::PutIntoPktList(
		enum FFPktOpe opeType,
		void *pPkt,
		int lockMutex)
{
	if(lockMutex)
		TFF_GetMutex(_hPktListMutex, TFF_INFINITE);

	FFPacketList *pNew = (FFPacketList *)av_mallocz(sizeof(FFPacketList));
	pNew->ope = opeType;
	pNew->pPkt = pPkt;

	FFPacketList *pList = _pPktList;
	if(pList == NULL)
		_pPktList = pNew;
	else
	{
		while(pList->next != NULL)
			pList = pList->next;
		pList->next = pNew;
	}

	_pPktList->count++;
	if(lockMutex)
		TFF_ReleaseMutex(_hPktListMutex);

	return 0;
}

int TFFmpegPacketer::FreePktList()
{
	TFF_GetMutex(_hPktListMutex, TFF_INFINITE);
	FFPacketList *pCur = _pPktList;
	FFPacketList *pNext = NULL;
	while(pCur != NULL)
	{
		pNext = pCur->next;
		FreeSinglePktList(&pCur);
		pCur = pNext;
	}
	_pPktList = NULL;
	TFF_ReleaseMutex(_hPktListMutex);
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