#include "TFFmpegDecoder.h"
#include "TFFmpegUtil.h"

#define FF_MAX_CACHED_FRAME_COUNT 3

TFFmpegDecoder::TFFmpegDecoder(FFContext *p, TFFmpegPacketer *pPkter)
	: _cmd(DecoderCmd_None),
	_isFinished(FALSE),
	_hEOFEvent(NULL),
	_pQ(NULL),
	_mutex(NULL),
	_hThread(NULL),
	_swsCtx(NULL)
{
	_pCtx = p;
	_pPkter = pPkter;
}
TFFmpegDecoder::~TFFmpegDecoder()
{
	_cmd |= DecoderCmd_Exit;
	TFF_SetEvent(_hEOFEvent);
	TFF_CondBroadcast(_pQ->cond);
	if(_hThread)
	{
		TFF_WaitThread(_hThread, 1000);
		CloseThreadP(&_hThread);
	}
	avcodec_free_frame(&_decodedFrame);
	DestroyFrameQueue();
	CloseEventP(&_hEOFEvent);
	CloseMutexP(&_mutex);
	if(_swsCtx)
	{
		sws_freeContext(_swsCtx);
		_swsCtx = NULL;
	}
}

int TFFmpegDecoder::Init()
{
	_hEOFEvent = TFF_CreateEvent(FALSE, FALSE);

	_pQ = (FFFrameQueue *)malloc(sizeof(FFFrameQueue));
	_pQ->mutex = TFF_CreateMutex();
	_pQ->cond = TFF_CreateCond();
	_pQ->count = 0;
	_pQ->first = _pQ->last = NULL;

	_mutex = TFF_CreateMutex();

	_decodedFrame = avcodec_alloc_frame();

	_swsCtx = sws_getCachedContext(
		NULL,
		_pCtx->pVideoStream->codec->width,
		_pCtx->pVideoStream->codec->height,
        _pCtx->pVideoStream->codec->pix_fmt,
        _pCtx->dstWidth,
		_pCtx->dstHeight,
		_pCtx->dstPixFmt,
        SWS_FAST_BILINEAR, NULL, NULL, NULL);
	return 0;
}

int TFFmpegDecoder::SetResolution(int width, int height)
{
	TFF_GetMutex(_pQ->mutex, TFF_INFINITE);
	_pCtx->dstWidth = width;
	_pCtx->dstHeight = height;
	_swsCtx = sws_getCachedContext(
		_swsCtx,
		_pCtx->pVideoStream->codec->width,
		_pCtx->pVideoStream->codec->height,
        _pCtx->pVideoStream->codec->pix_fmt,
        _pCtx->dstWidth,
		_pCtx->dstHeight,
		_pCtx->dstPixFmt,
        SWS_FAST_BILINEAR, NULL, NULL, NULL);
	TFF_ReleaseMutex(_pQ->mutex);
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
	TFF_GetMutex(_pQ->mutex, TFF_INFINITE);
	int count = _pQ->count;
	TFF_ReleaseMutex(_pQ->mutex);
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
	TFF_GetMutex(_mutex, TFF_INFINITE);
	TFF_GetMutex(_pQ->mutex, TFF_INFINITE);
	_pPkter->SeekPos(pos);
	Flush(pos);
	_cmd |= DecoderCmd_Abandon;
	_isFinished = FALSE;
	TFF_CondBroadcast(_pQ->cond);
	TFF_SetEvent(_hEOFEvent);
	TFF_ReleaseMutex(_pQ->mutex);
	TFF_ReleaseMutex(_mutex);
	return 0;
}

int TFFmpegDecoder::PutIntoFrameList(AVPacket *pPkt, int64_t pdts)
{
	FFFrameList *pFrame = NULL;
	AllocDstFrame(&pFrame);
	pFrame->ope = FrameOpe_None;
	sws_scale(_swsCtx,
		_decodedFrame->data,
		_decodedFrame->linesize,
		0,
		_decodedFrame->height,
		pFrame->pFrame->data,
		pFrame->pFrame->linesize);

	if(pPkt->dts == AV_NOPTS_VALUE)
		pFrame->pFrame->pkt_dts = pdts;
	else
		pFrame->pFrame->pkt_dts = pPkt->dts;

	return PutIntoFrameList(pFrame);
}

int TFFmpegDecoder::Flush(int64_t pos, BOOL seekToPos)
{
	DebugOutput("TFFmpegDecoder::Flush Flush packet.");
	ClearFrameQueue();
	avcodec_flush_buffers(_pCtx->pVideoStream->codec);
	FFPacketList *pTmpPktList = NULL;

	while(seekToPos)
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
			_decodedFrame, &gotPic, pTmpPkt);
		if(!gotPic)
			tmpPtds = pTmpPkt->dts;
		if(pTmpPkt->dts >= pos
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

		FFPacketList *pPktList = NULL;
		TFF_GetMutex(_mutex, TFF_INFINITE);
		_cmd &= ~DecoderCmd_Abandon;
		if(_pPkter->GetPacket(&pPktList) >= 0)
		{
			int gotPic = 0;
			AVPacket *pkt = (AVPacket *)pPktList->pPkt;
			avcodec_decode_video2(_pCtx->pVideoStream->codec,
				_decodedFrame, &gotPic, pkt);
			TFF_ReleaseMutex(_mutex);
			if(gotPic)
			{
				TFF_GetMutex(_pQ->mutex, TFF_INFINITE);

				while(_pQ->count >= FF_MAX_CACHED_FRAME_COUNT &&
					!(_cmd & DecoderCmd_Abandon) &&
					!(_cmd & DecoderCmd_Exit))
					TFF_WaitCond(_pQ->cond, _pQ->mutex);

				if(!(_cmd & DecoderCmd_Abandon) &&
					!(_cmd & DecoderCmd_Exit))
				{
					PutIntoFrameList(pkt, pdts);
					TFF_CondBroadcast(_pQ->cond);
				}
				TFF_ReleaseMutex(_pQ->mutex);
			}
			else
				pdts = pkt->dts;
			_pPkter->FreeSinglePktList(&pPktList);
		}
		else
		{
			TFF_ReleaseMutex(_mutex);
			_isFinished = TRUE;
			TFF_CondBroadcast(_pQ->cond);
			DebugOutput("TFFmpegDecoder::Thread Finished. Will wait for awake.");
			TFF_WaitEvent(_hEOFEvent, TFF_INFINITE);
		}
	}
	DebugOutput("TFFmpegDecoder::Thread exit.");
}

int TFFmpegDecoder::Get(FFFrameList **pFrameList)
{
	TFF_GetMutex(_pQ->mutex, TFF_INFINITE);
	*pFrameList = _pQ->first;
	TFF_ReleaseMutex(_pQ->mutex);
	return 0;
}

int TFFmpegDecoder::Pop(FFFrameList **pFrameList)
{
	int ret = FF_OK;
	TFF_GetMutex(_pQ->mutex, TFF_INFINITE);

	while(_pQ->count == 0 &&
		!_isFinished)
		TFF_WaitCond(_pQ->cond, _pQ->mutex);

	if(_isFinished)
	{
		*pFrameList = NULL;
		ret = -1;
	}
	else
	{
		*pFrameList = _pQ->first;
		_pQ->first = _pQ->first->next;
		_pQ->count--;
		TFF_CondBroadcast(_pQ->cond);
	}

	TFF_ReleaseMutex(_pQ->mutex);
	return ret;
}

int TFFmpegDecoder::PutIntoFrameList(FFFrameList *pNew)
{
	if(_pQ->count == 0)
		_pQ->first = _pQ->last = pNew;
	else
	{
		_pQ->last->next = pNew;
		_pQ->last = pNew;
	}

	_pQ->count++;
	return FF_OK;
}

int TFFmpegDecoder::DestroyFrameQueue()
{
	ClearFrameQueue();
	TFF_CloseMutex(_pQ->mutex);
	TFF_DestroyCond(&_pQ->cond);
	free(_pQ);
	_pQ = NULL;
	return FF_OK;
}

int TFFmpegDecoder::ClearFrameQueue()
{
	FFFrameList *pCur = _pQ->first;
	FFFrameList *pNext = NULL;
	while(pCur != NULL)
	{
		pNext = pCur->next;
		FreeSingleFrameList(&pCur);
		pCur = pNext;
	}
	_pQ->first = _pQ->last = NULL;
	_pQ->count = 0;
	return FF_OK;
}

int TFFmpegDecoder::FreeSingleFrameList(FFFrameList **ppFrameList)
{
	FFFrameList *pFrameList = *ppFrameList;
	if(!pFrameList)
		return FF_ERR_NOPOINTER;
	if(pFrameList->pFrame)
		avcodec_free_frame(&pFrameList->pFrame);
	if(pFrameList->buffer)
		av_free(pFrameList->buffer);
	av_free(pFrameList);
	*ppFrameList = NULL;
	return FF_OK;
}

int TFFmpegDecoder::AllocDstFrame(OUT FFFrameList **ppDstFrame)
{
	FFFrameList *pFrame = (FFFrameList *)av_malloc(sizeof(FFFrameList));
	pFrame->next = NULL;
	pFrame->pFrame = avcodec_alloc_frame();
	int numBytes=avpicture_get_size(_pCtx->dstPixFmt,
		_pCtx->dstWidth,
		_pCtx->dstHeight);
	pFrame->buffer = (uint8_t *)av_malloc(numBytes*sizeof(uint8_t));
	avpicture_fill((AVPicture *)pFrame->pFrame,
		pFrame->buffer,
		_pCtx->dstPixFmt,
		_pCtx->dstWidth,
		_pCtx->dstHeight);
	pFrame->width = _pCtx->dstWidth;
	pFrame->height = _pCtx->dstHeight;
	pFrame->size = numBytes;

	*ppDstFrame = pFrame;
	return 0;
}