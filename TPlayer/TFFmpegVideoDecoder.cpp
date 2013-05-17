#include "TFFmpegVideoDecoder.h"
#include "TFFmpegUtil.h"

#define FF_MAX_CACHED_FRAME_COUNT 3

//32MB
#define FF_MAX_CACHED_FRAME_SIZE 0x02000000

TFFmpegVideoDecoder::TFFmpegVideoDecoder(FFContext *p, TFFmpegPacketer *pPkter)
	: _cmd(DecoderCmd_None),
	_isFinished(FALSE),
	_hEOFEvent(NULL),
	_pVQ(NULL),
	_mutex(NULL),
	_hThread(NULL),
	_swsCtx(NULL)
{
	_pCtx = p;
	_pPkter = pPkter;
}
TFFmpegVideoDecoder::~TFFmpegVideoDecoder()
{
	_cmd |= DecoderCmd_Exit;
	TFF_SetEvent(_hEOFEvent);
	TFF_CondBroadcast(_pVQ->cond);
	if(_hThread)
	{
		TFF_WaitThread(_hThread, 1000);
		CloseThreadP(&_hThread);
	}
	DestroyFrameQueue();
	CloseEventP(&_hEOFEvent);
	CloseMutexP(&_mutex);
	if(_swsCtx)
	{
		sws_freeContext(_swsCtx);
		_swsCtx = NULL;
	}
}

int TFFmpegVideoDecoder::Init()
{
	_hEOFEvent = TFF_CreateEvent(FALSE, FALSE);

	_pVQ = (FFFrameQueue *)malloc(sizeof(FFFrameQueue));
	_pVQ->mutex = TFF_CreateMutex();
	_pVQ->cond = TFF_CreateCond();
	_pVQ->count = 0;
	_pVQ->size = 0;
	_pVQ->first = _pVQ->last = NULL;

	_mutex = TFF_CreateMutex();

	_swsCtx = sws_getCachedContext(
		NULL,
		_pCtx->videoStream->codec->width,
		_pCtx->videoStream->codec->height,
        _pCtx->videoStream->codec->pix_fmt,
        _pCtx->dstWidth,
		_pCtx->dstHeight,
		_pCtx->dstPixFmt,
        SWS_FAST_BILINEAR, NULL, NULL, NULL);
	return 0;
}

int TFFmpegVideoDecoder::SetResolution(int width, int height)
{
	TFF_GetMutex(_pVQ->mutex, TFF_INFINITE);
	_pCtx->dstWidth = width;
	_pCtx->dstHeight = height;
	_swsCtx = sws_getCachedContext(
		_swsCtx,
		_pCtx->videoStream->codec->width,
		_pCtx->videoStream->codec->height,
        _pCtx->videoStream->codec->pix_fmt,
        _pCtx->dstWidth,
		_pCtx->dstHeight,
		_pCtx->dstPixFmt,
        SWS_FAST_BILINEAR, NULL, NULL, NULL);
	TFF_ReleaseMutex(_pVQ->mutex);
	return 0;
}

int TFFmpegVideoDecoder::Start()
{
	if(!_hThread)
		_hThread = TFF_CreateThread(SThreadStart, this);
	return 0;
}

int TFFmpegVideoDecoder::GetFrameCount()
{
	TFF_GetMutex(_pVQ->mutex, TFF_INFINITE);
	int count = _pVQ->count;
	TFF_ReleaseMutex(_pVQ->mutex);
	return count;
}

unsigned long __stdcall TFFmpegVideoDecoder::SThreadStart(void *p)
{
	TFFmpegVideoDecoder *pThis = (TFFmpegVideoDecoder *)p;
	pThis->ThreadStart();
	return 0;
}

int TFFmpegVideoDecoder::SeekPos(int64_t pos)
{
	TFF_GetMutex(_mutex, TFF_INFINITE);
	TFF_GetMutex(_pVQ->mutex, TFF_INFINITE);
	_pPkter->SeekPos(pos);
	Flush(pos);
	_cmd |= DecoderCmd_Abandon;
	_isFinished = FALSE;
	TFF_CondBroadcast(_pVQ->cond);
	TFF_SetEvent(_hEOFEvent);
	TFF_ReleaseMutex(_pVQ->mutex);
	TFF_ReleaseMutex(_mutex);
	return 0;
}

int TFFmpegVideoDecoder::PutIntoFrameList(AVFrame *pDecVFrame, int64_t pktDts, int64_t pdts)
{
	FFFrameList *pFrame = NULL;
	AllocDstFrame(&pFrame);
	pFrame->ope = FrameOpe_None;
	int ret = sws_scale(_swsCtx,
		pDecVFrame->data,
		pDecVFrame->linesize,
		0,
		pDecVFrame->height,
		pFrame->pFrame->data,
		pFrame->pFrame->linesize);

	pFrame->pFrame->pkt_dts = (pktDts == AV_NOPTS_VALUE) ? pdts : pktDts;
	return PutIntoFrameList(pFrame);
}

int TFFmpegVideoDecoder::Flush(int64_t pos, BOOL seekToPos)
{
	DebugOutput("TFFmpegVideoDecoder::Flush Flush packet.");
	ClearFrameQueue();
	avcodec_flush_buffers(_pCtx->videoStream->codec);
	FFPacketList *pTmpPktList = NULL;

	AVFrame *decVFrame = avcodec_alloc_frame();
	while(seekToPos)
	{
		if (_pPkter->GetVideoPacket(&pTmpPktList) < 0)
		{
			DebugOutput("TFFmpegVideoDecoder::Flush finished when flush.");
			break;
		}
		AVPacket *pTmpPkt = (AVPacket *)pTmpPktList->pPkt;
		int gotPic = 0;
		int64_t tmpPtds = -1;
		avcodec_decode_video2(_pCtx->videoStream->codec,
			decVFrame, &gotPic, pTmpPkt);
		if(!gotPic)
			tmpPtds = pTmpPkt->dts;
		if(pTmpPkt->dts >= pos
			&& gotPic)
		{
			DebugOutput("TFFmpegVideoDecoder::Flush Seek to position.");
			PutIntoFrameList(decVFrame, pTmpPkt->dts, tmpPtds);
			_pPkter->FreeSinglePktList(&pTmpPktList);
			break;
		}
		_pPkter->FreeSinglePktList(&pTmpPktList);
	}
	avcodec_free_frame(&decVFrame);
	return 0;
}

BOOL TFFmpegVideoDecoder::IsFinished()
{
	return _isFinished;
}

void __stdcall TFFmpegVideoDecoder::ThreadStart()
{
	DebugOutput("TFFmpegVideoDecoder::Thread DecodeThread begin.");
	int64_t pdts = -1;
	AVFrame *decVFrame = avcodec_alloc_frame();
	while(true)
	{
		if(_cmd & DecoderCmd_Exit)
			break;

		FFPacketList *pPktList = NULL;
		TFF_GetMutex(_mutex, TFF_INFINITE);
		_cmd &= ~DecoderCmd_Abandon;
		if(_pPkter->GetVideoPacket(&pPktList) >= 0)
		{
			int gotPic = 0;
			AVPacket *pkt = (AVPacket *)pPktList->pPkt;
			avcodec_decode_video2(_pCtx->videoStream->codec,
				decVFrame, &gotPic, pkt);
			TFF_ReleaseMutex(_mutex);
			if(gotPic)
			{
				TFF_GetMutex(_pVQ->mutex, TFF_INFINITE);

				while(_pVQ->count >= FF_MAX_CACHED_FRAME_COUNT &&
					!(_cmd & DecoderCmd_Abandon) &&
					!(_cmd & DecoderCmd_Exit))
					TFF_WaitCond(_pVQ->cond, _pVQ->mutex);

				if(!(_cmd & DecoderCmd_Abandon) &&
					!(_cmd & DecoderCmd_Exit))
				{
					PutIntoFrameList(decVFrame, pkt->dts, pdts);
					TFF_CondBroadcast(_pVQ->cond);
				}
				TFF_ReleaseMutex(_pVQ->mutex);
			}
			else
				pdts = pkt->dts;

			_pPkter->FreeSinglePktList(&pPktList);
		}
		else
		{
			TFF_ReleaseMutex(_mutex);
			_isFinished = TRUE;
			TFF_CondBroadcast(_pVQ->cond);
			DebugOutput("TFFmpegVideoDecoder::Thread Finished. Will wait for awake.");
			TFF_WaitEvent(_hEOFEvent, TFF_INFINITE);
		}
	}
	avcodec_free_frame(&decVFrame);
	DebugOutput("TFFmpegVideoDecoder::Thread exit.");
}

int TFFmpegVideoDecoder::Get(FFFrameList **pFrameList)
{
	TFF_GetMutex(_pVQ->mutex, TFF_INFINITE);
	*pFrameList = _pVQ->first;
	TFF_ReleaseMutex(_pVQ->mutex);
	return 0;
}

int TFFmpegVideoDecoder::Pop(FFFrameList **pFrameList)
{
	int ret = FF_OK;
	TFF_GetMutex(_pVQ->mutex, TFF_INFINITE);

	while(_pVQ->count == 0 &&
		!_isFinished)
		TFF_WaitCond(_pVQ->cond, _pVQ->mutex);

	if(_isFinished)
	{
		*pFrameList = NULL;
		ret = -1;
	}
	else
	{
		*pFrameList = _pVQ->first;
		_pVQ->size -= _pVQ->first->size;
		_pVQ->first = _pVQ->first->next;
		_pVQ->count--;
		TFF_CondBroadcast(_pVQ->cond);
	}

	TFF_ReleaseMutex(_pVQ->mutex);
	return ret;
}

int TFFmpegVideoDecoder::PutIntoFrameList(FFFrameList *pNew)
{
	if(_pVQ->count == 0)
		_pVQ->first = _pVQ->last = pNew;
	else
	{
		_pVQ->last->next = pNew;
		_pVQ->last = pNew;
	}

	_pVQ->count++;
	_pVQ->size += pNew->size;
	return FF_OK;
}

int TFFmpegVideoDecoder::DestroyFrameQueue()
{
	ClearFrameQueue();
	TFF_CloseMutex(_pVQ->mutex);
	TFF_DestroyCond(&_pVQ->cond);
	free(_pVQ);
	_pVQ = NULL;
	return FF_OK;
}

int TFFmpegVideoDecoder::ClearFrameQueue()
{
	FFFrameList *pCur = _pVQ->first;
	FFFrameList *pNext = NULL;
	while(pCur != NULL)
	{
		pNext = pCur->next;
		FreeSingleFrameList(&pCur);
		pCur = pNext;
	}
	_pVQ->first = _pVQ->last = NULL;
	_pVQ->count = 0;
	_pVQ->size = 0;
	return FF_OK;
}

int TFFmpegVideoDecoder::FreeSingleFrameList(FFFrameList **ppFrameList)
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

int TFFmpegVideoDecoder::AllocDstFrame(OUT FFFrameList **ppDstFrame)
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