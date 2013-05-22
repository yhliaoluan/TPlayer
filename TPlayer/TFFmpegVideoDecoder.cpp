#include "TFFmpegVideoDecoder.h"
#include "TFFmpegUtil.h"

#define FF_MAX_CACHED_FRAME_COUNT 5

//32MB
#define FF_MAX_CACHED_FRAME_SIZE 0x02000000

TFFmpegVideoDecoder::TFFmpegVideoDecoder(FFContext *p, TFFmpegPacketer *pPkter)
	: _cmd(DecoderCmd_None),
	_isFinished(FALSE),
	_hEOFEvent(NULL),
	_queue(NULL),
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
	TFF_CondBroadcast(_queue->cond);
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

	_queue = (FFFrameQueue *)malloc(sizeof(FFFrameQueue));
	_queue->mutex = TFF_CreateMutex();
	_queue->cond = TFF_CreateCond();
	_queue->count = 0;
	_queue->size = 0;
	_queue->firstV = _queue->lastV = NULL;

	_mutex = TFF_CreateMutex();

	_swsCtx = sws_getCachedContext(
		NULL,
		_pCtx->videoStream->codec->width,
		_pCtx->videoStream->codec->height,
        _pCtx->videoStream->codec->pix_fmt,
        _pCtx->width,
		_pCtx->height,
		_pCtx->pixFmt,
        SWS_FAST_BILINEAR, NULL, NULL, NULL);
	return 0;
}

int TFFmpegVideoDecoder::SetResolution(int width, int height)
{
	TFF_GetMutex(_queue->mutex, TFF_INFINITE);
	_pCtx->width = width;
	_pCtx->height = height;
	_swsCtx = sws_getCachedContext(
		_swsCtx,
		_pCtx->videoStream->codec->width,
		_pCtx->videoStream->codec->height,
        _pCtx->videoStream->codec->pix_fmt,
        _pCtx->width,
		_pCtx->height,
		_pCtx->pixFmt,
        SWS_FAST_BILINEAR, NULL, NULL, NULL);
	TFF_ReleaseMutex(_queue->mutex);
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
	TFF_GetMutex(_queue->mutex, TFF_INFINITE);
	int count = _queue->count;
	TFF_ReleaseMutex(_queue->mutex);
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
	TFF_GetMutex(_queue->mutex, TFF_INFINITE);
	_pPkter->SeekPos(pos);
	Flush(pos);
	_cmd |= DecoderCmd_Abandon;
	_isFinished = FALSE;
	TFF_CondBroadcast(_queue->cond);
	TFF_SetEvent(_hEOFEvent);
	TFF_ReleaseMutex(_queue->mutex);
	TFF_ReleaseMutex(_mutex);
	return 0;
}

int TFFmpegVideoDecoder::PutIntoFrameList(AVFrame *pDecVFrame, int64_t pktDts, int64_t pdts)
{
	FFVideoFrame *pFrame = NULL;
	AllocDstFrame(&pFrame);
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
	AVFrame *decVFrame = NULL;
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
			if(!decVFrame)
				decVFrame = avcodec_alloc_frame();
			else
				avcodec_get_frame_defaults(decVFrame);

			avcodec_decode_video2(_pCtx->videoStream->codec,
				decVFrame, &gotPic, pkt);
			TFF_ReleaseMutex(_mutex);
			if(gotPic)
			{
				TFF_GetMutex(_queue->mutex, TFF_INFINITE);

				while(_queue->count >= FF_MAX_CACHED_FRAME_COUNT &&
					!(_cmd & DecoderCmd_Abandon) &&
					!(_cmd & DecoderCmd_Exit))
					TFF_WaitCond(_queue->cond, _queue->mutex);

				if(!(_cmd & DecoderCmd_Abandon) &&
					!(_cmd & DecoderCmd_Exit))
				{
					PutIntoFrameList(decVFrame, pkt->dts, pdts);
				}
				TFF_ReleaseMutex(_queue->mutex);
			}
			else
				pdts = pkt->dts;

			_pPkter->FreeSinglePktList(&pPktList);
		}
		else
		{
			TFF_ReleaseMutex(_mutex);
			_isFinished = TRUE;
			TFF_CondBroadcast(_queue->cond);
			DebugOutput("TFFmpegVideoDecoder::Thread Finished. Will wait for awake.");
			TFF_WaitEvent(_hEOFEvent, TFF_INFINITE);
		}
	}
	avcodec_free_frame(&decVFrame);
	DebugOutput("TFFmpegVideoDecoder::Thread exit.");
}

int TFFmpegVideoDecoder::Get(FFVideoFrame **pFrameList)
{
	TFF_GetMutex(_queue->mutex, TFF_INFINITE);
	*pFrameList = _queue->firstV;
	TFF_ReleaseMutex(_queue->mutex);
	return 0;
}

int TFFmpegVideoDecoder::Pop(FFVideoFrame **pFrameList)
{
	int ret = FF_OK;
	TFF_GetMutex(_queue->mutex, TFF_INFINITE);

	while(_queue->count == 0 &&
		!_isFinished)
		TFF_WaitCond(_queue->cond, _queue->mutex);

	if(_isFinished)
	{
		*pFrameList = NULL;
		ret = -1;
	}
	else
	{
		*pFrameList = _queue->firstV;
		_queue->size -= _queue->firstV->size;
		_queue->firstV = _queue->firstV->next;
		_queue->count--;
		if(_queue->count <= 1)
			TFF_CondBroadcast(_queue->cond);
	}

	TFF_ReleaseMutex(_queue->mutex);
	return ret;
}

int TFFmpegVideoDecoder::PutIntoFrameList(FFVideoFrame *pNew)
{
	if(_queue->count == 0)
		_queue->firstV = _queue->lastV = pNew;
	else
	{
		_queue->lastV->next = pNew;
		_queue->lastV = pNew;
	}

	_queue->count++;
	_queue->size += pNew->size;

	if(_queue->count == 1)
		TFF_CondBroadcast(_queue->cond);
	return FF_OK;
}

int TFFmpegVideoDecoder::DestroyFrameQueue()
{
	ClearFrameQueue();
	TFF_CloseMutex(_queue->mutex);
	TFF_DestroyCond(&_queue->cond);
	free(_queue);
	_queue = NULL;
	return FF_OK;
}

int TFFmpegVideoDecoder::ClearFrameQueue()
{
	FFVideoFrame *pCur = _queue->firstV;
	FFVideoFrame *pNext = NULL;
	while(pCur != NULL)
	{
		pNext = pCur->next;
		FreeSingleFrameList(&pCur);
		pCur = pNext;
	}
	_queue->firstV = _queue->lastV = NULL;
	_queue->count = 0;
	_queue->size = 0;
	return FF_OK;
}

int TFFmpegVideoDecoder::FreeSingleFrameList(FFVideoFrame **ppFrameList)
{
	FFVideoFrame *pFrameList = *ppFrameList;
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

int TFFmpegVideoDecoder::AllocDstFrame(OUT FFVideoFrame **ppDstFrame)
{
	FFVideoFrame *pFrame = (FFVideoFrame *)av_malloc(sizeof(FFVideoFrame));
	pFrame->next = NULL;
	pFrame->pFrame = avcodec_alloc_frame();
	int numBytes=avpicture_get_size(_pCtx->pixFmt,
		_pCtx->width,
		_pCtx->height);
	pFrame->buffer = (uint8_t *)av_malloc(numBytes*sizeof(uint8_t));
	avpicture_fill((AVPicture *)pFrame->pFrame,
		pFrame->buffer,
		_pCtx->pixFmt,
		_pCtx->width,
		_pCtx->height);
	pFrame->width = _pCtx->width;
	pFrame->height = _pCtx->height;
	pFrame->size = numBytes;

	*ppDstFrame = pFrame;
	return 0;
}