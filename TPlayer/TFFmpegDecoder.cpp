#include "TFFmpegDecoder.h"
#include "TFFmpegUtil.h"

#define FF_MAX_CACHED_FRAME_COUNT 3

TFFmpegDecoder::TFFmpegDecoder(FFContext *p, TFFmpegPacketer *pPkter)
	: _hThread(NULL)
	, _cmd(DecoderCmd_None),
	_pFrameList(NULL),
	_isFinished(FALSE)
{
	_pCtx = p;
	_pPkter = pPkter;
}
TFFmpegDecoder::~TFFmpegDecoder()
{
	_cmd |= DecoderCmd_Exit;
	TFF_SetEvent(_hGetFrameEvent);
	TFF_SetEvent(_hEOFEvent);
	if(_hThread)
	{
		TFF_WaitThread(_hThread, 1000);
		CloseThreadP(&_hThread);
	}
	FreeFrameList();
	CloseMutexP(&_hFrameListMutex);
	CloseEventP(&_hGetFrameEvent);
	CloseEventP(&_hEOFEvent);
	CloseEventP(&_hSyncEvent);
}

int TFFmpegDecoder::Init()
{
	_hFrameListMutex = TFF_CreateMutex();
	_hGetFrameEvent = TFF_CreateEvent();
	_hEOFEvent = TFF_CreateEvent();
	_hSyncEvent = TFF_CreateEvent();
	return 0;
}

int TFFmpegDecoder::Start()
{
	if(!_hThread)
		_hThread = TFF_CreateThread(SThreadStart, this);
	return 0;
}

int TFFmpegDecoder::GetFrameCount()
{
	TFF_GetMutex(_hFrameListMutex, TFF_INFINITE);
	int count = 0;
	if(_pFrameList != NULL)
		count = _pFrameList->count;
	TFF_ReleaseMutex(_hFrameListMutex);

	return count;
}

unsigned long __stdcall TFFmpegDecoder::SThreadStart(void *p)
{
	TFFmpegDecoder *pThis = (TFFmpegDecoder *)p;
	pThis->ThreadStart();
	return 0;
}

int TFFmpegDecoder::SeekPos(int64_t pos)
{
	_pPkter->SeekPos(pos);
	TFF_SetEvent(_hGetFrameEvent);
	TFF_SetEvent(_hEOFEvent);
	TFF_WaitEvent(_hSyncEvent, TFF_INFINITE);
	return 0;
}

int TFFmpegDecoder::PutIntoFrameList(AVPacket *pPkt, int64_t pdts)
{
	AVFrame *pFrameRGB = NULL;
	uint8_t *buffer = NULL;
	AllocRGBFrame(&pFrameRGB, &buffer);
	sws_scale(_pCtx->pSwsCtx,
		_pCtx->pDecodedFrame->data, _pCtx->pDecodedFrame->linesize, 0,
		_pCtx->pDecodedFrame->height, pFrameRGB->data,
		pFrameRGB->linesize);

	if(pPkt->dts == AV_NOPTS_VALUE)
		pFrameRGB->pkt_dts = pdts;
	else
		pFrameRGB->pkt_dts = pPkt->dts;

	return PutIntoFrameList(pFrameRGB, buffer, FrameOpe_None);
}

int TFFmpegDecoder::Flush(FFPacketList *pPktList)
{
	DebugOutput("TFFmpegDecoder::Flush Flush packet.");
	FreeFrameList();
	avcodec_flush_buffers(_pCtx->pVideoStream->codec);
	FFSeekPosPkt *pSeekPkt = (FFSeekPosPkt *)pPktList->pPkt;
	FFPacketList *pTmpPktList = NULL;

	while(true)
	{
		if (_pPkter->GetPacket(&pTmpPktList) < 0)
		{
			if (_pPkter->IsFinished())
			{
				DebugOutput("TFFmpegDecoder::Flush finished when flush.");
				break;
			}
			DebugOutput("TFFmpegDecoder::Flush no packet. Will wait in 10 ms.");
			TFF_Sleep(10);
			continue;
		}
		AVPacket *pTmpPkt = (AVPacket *)pTmpPktList->pPkt;
		int gotPic = 0;
		int64_t tmpPtds = -1;
		avcodec_decode_video2(_pCtx->pVideoStream->codec,
			_pCtx->pDecodedFrame, &gotPic, pTmpPkt);
		if(!gotPic)
			tmpPtds = pTmpPkt->dts;
		if(pTmpPkt->dts >= pSeekPkt->pos
			&& gotPic)
		{
			DebugOutput("TFFmpegDecoder::Flush Seek to position.");
			PutIntoFrameList(pTmpPkt, tmpPtds);
			_pPkter->FreeSinglePktList(&pTmpPktList);
			break;
		}
		_pPkter->FreeSinglePktList(&pTmpPktList);
	}
	return 0;
}

BOOL TFFmpegDecoder::IsFinished()
{
	return _isFinished;
}

void __stdcall TFFmpegDecoder::ThreadStart()
{
	DebugOutput("TFFmpegDecoder::Thread DecodeThread begin.");
	int64_t pdts = -1;
	while(true)
	{
		if(_cmd & DecoderCmd_Exit)
			break;

		enum FFPktOpe ope;
		BOOL flush = FALSE;
		if(_pPkter->GetFirstPktOpe(&ope) >= 0)
			flush = (ope == PktOpe_Flush);

		if(!flush && GetFrameCount() >= FF_MAX_CACHED_FRAME_COUNT)
		{
			TFF_WaitEvent(_hGetFrameEvent, TFF_INFINITE);
			continue;
		}

		FFPacketList *pPktList = NULL;
		if(_pPkter->GetPacket(&pPktList) < 0)
		{
			if(_pPkter->IsFinished())
			{
				_isFinished = TRUE;
				DebugOutput("TFFmpegDecoder::Thread Finished. Will wait for awake.");
				TFF_WaitEvent(_hEOFEvent, TFF_INFINITE);
				_isFinished = FALSE;
			}
			DebugOutput("TFFmpegDecoder::Thread Cannot get a packet. Will try in 10 ms.");
			TFF_Sleep(10);
			continue;
		}
		if (pPktList->ope == PktOpe_Flush)
		{
			Flush(pPktList);
			DebugOutput("TFFmpegDecoder::Thread Notify wait event.");
			TFF_SetEvent(_hSyncEvent);
			_pPkter->FreeSinglePktList(&pPktList);
			continue;
		}
		else if(pPktList->ope == PktOpe_None)
		{
			int gotPic = 0;
			AVPacket *pkt = (AVPacket *)pPktList->pPkt;
			avcodec_decode_video2(_pCtx->pVideoStream->codec,
				_pCtx->pDecodedFrame, &gotPic, pkt);
			if(gotPic)
				PutIntoFrameList(pkt, pdts);
			else
				pdts = pkt->dts;
			_pPkter->FreeSinglePktList(&pPktList);
		}
	}
	DebugOutput("TFFmpegDecoder::Thread exit.");
}

int TFFmpegDecoder::Get(FFFrameList **pFrameList)
{
	int ret = 0;
	TFF_GetMutex(_hFrameListMutex, TFF_INFINITE);

	if(_pFrameList == NULL)
	{
		*pFrameList = NULL;
		ret = -1;
	}
	else
		*pFrameList = _pFrameList;
	TFF_ReleaseMutex(_hFrameListMutex);
	return ret;
}

int TFFmpegDecoder::Pop(FFFrameList **pFrameList)
{
	int ret = 0;
	TFF_GetMutex(_hFrameListMutex, TFF_INFINITE);

	if(_pFrameList == NULL)
	{
		*pFrameList = NULL;
		ret = -1;
	}
	else
	{
		*pFrameList = _pFrameList;
		_pFrameList = _pFrameList->next;
		if((*pFrameList)->count > 1)
			_pFrameList->count = (*pFrameList)->count - 1;
	}

	TFF_ReleaseMutex(_hFrameListMutex);

	if(ret >= 0)
		TFF_SetEvent(_hGetFrameEvent);
	return ret;
}

int TFFmpegDecoder::PutIntoFrameList(
		AVFrame *pFrameRGB,
		uint8_t *pBuffer,
		enum FFFrameOpe ope,
		int lockMutex)
{
	if(lockMutex)
		TFF_GetMutex(_hFrameListMutex, TFF_INFINITE);

	FFFrameList *pNew = (FFFrameList *)av_mallocz(sizeof(FFFrameList));
	pNew->pFrame = pFrameRGB;
	pNew->buffer = pBuffer;
	pNew->ope = ope;

	FFFrameList *pList = _pFrameList;
	if(pList == NULL)
		_pFrameList = pNew;
	else
	{
		while(pList->next != NULL)
			pList = pList->next;
		pList->next = pNew;
	}

	_pFrameList->count++;
	if(lockMutex)
		TFF_ReleaseMutex(_hFrameListMutex);
	return 0;
}

int TFFmpegDecoder::FreeFrameList()
{
	TFF_GetMutex(_hFrameListMutex, TFF_INFINITE);
	FFFrameList *pCur = _pFrameList;
	FFFrameList *pNext = NULL;
	while(pCur != NULL)
	{
		pNext = pCur->next;
		FreeSingleFrameList(&pCur);
		pCur = pNext;
	}
	_pFrameList = NULL;
	TFF_ReleaseMutex(_hFrameListMutex);
	return 0;
}

int TFFmpegDecoder::FreeSingleFrameList(FFFrameList **ppFrameList)
{
	FFFrameList *pFrameList = *ppFrameList;
	if(pFrameList->pFrame)
		avcodec_free_frame(&pFrameList->pFrame);
	if(pFrameList->buffer)
		av_free(pFrameList->buffer);
	av_free(pFrameList);
	*ppFrameList = NULL;
	return 0;
}

int TFFmpegDecoder::AllocRGBFrame(
	OUT AVFrame **ppFrameRGB,
	OUT uint8_t **ppBuffer)
{
	AVFrame *pFrameRGB = avcodec_alloc_frame();
	int numBytes=avpicture_get_size(PIX_FMT_RGB24,
		_pCtx->pVideoStream->codec->width,  
        _pCtx->pVideoStream->codec->height);
	uint8_t *buffer = (uint8_t *)av_malloc(numBytes*sizeof(uint8_t));
	avpicture_fill((AVPicture *)pFrameRGB,
		buffer,
		PIX_FMT_RGB24,  
		_pCtx->pVideoStream->codec->width,
		_pCtx->pVideoStream->codec->height);

	*ppFrameRGB = pFrameRGB;
	*ppBuffer = buffer;
	return 0;
}