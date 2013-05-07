#include "TFFmpegDecoder.h"
#include "TFFmpegUtil.h"

#define FF_MAX_CACHED_FRAME_COUNT 3

TFFmpegDecoder::TFFmpegDecoder(FFContext *p, TFFmpegPacketer *pPkter)
	: _cmd(DecoderCmd_None),
	_isFinished(FALSE),
	_hEOFEvent(NULL),
	_pQ(NULL),
	_mutex(NULL),
	_hThread(NULL)
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
	DestroyFrameQueue();
	CloseEventP(&_hEOFEvent);
	CloseMutexP(&_mutex);
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
	TFF_CondBroadcast(_pQ->cond);
	TFF_SetEvent(_hEOFEvent);
	TFF_ReleaseMutex(_pQ->mutex);
	TFF_ReleaseMutex(_mutex);
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
			_pCtx->pDecodedFrame, &gotPic, pTmpPkt);
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
				_pCtx->pDecodedFrame, &gotPic, pkt);
			TFF_ReleaseMutex(_mutex);
			if(gotPic)
			{
				TFF_GetMutex(_pQ->mutex, TFF_INFINITE);

				while(_pQ->count >= FF_MAX_CACHED_FRAME_COUNT &&
					(_cmd & DecoderCmd_Abandon) == 0 &&
					(_cmd & DecoderCmd_Exit) == 0)
					TFF_WaitCond(_pQ->cond, _pQ->mutex);

				if((_cmd & DecoderCmd_Abandon) == 0 &&
					(_cmd & DecoderCmd_Exit) == 0)
					PutIntoFrameList(pkt, pdts);
				TFF_ReleaseMutex(_pQ->mutex);
			}
			else
				pdts = pkt->dts;
			_pPkter->FreeSinglePktList(&pPktList);
		}
		else
		{
			TFF_ReleaseMutex(_mutex);
			if(_pPkter->IsFinished())
			{
				_isFinished = TRUE;
				DebugOutput("TFFmpegDecoder::Thread Finished. Will wait for awake.");
				TFF_WaitEvent(_hEOFEvent, TFF_INFINITE);
				_isFinished = FALSE;
			}
			else
			{
				DebugOutput("TFFmpegDecoder::Thread Cannot get a packet. Will try in 10 ms.");
				TFF_Sleep(10);
			}
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
	int ret = 0;
	TFF_GetMutex(_pQ->mutex, TFF_INFINITE);

	if(_pQ->count == 0)
	{
		*pFrameList = NULL;
		ret = -1;
	}
	else
	{
		*pFrameList = _pQ->first;
		_pQ->first = _pQ->first->next;
		_pQ->count--;
	}

	TFF_ReleaseMutex(_pQ->mutex);

	if(ret >= 0)
		TFF_CondBroadcast(_pQ->cond);
	return ret;
}

int TFFmpegDecoder::PutIntoFrameList(
		AVFrame *pFrameRGB,
		uint8_t *pBuffer,
		enum FFFrameOpe ope)
{
	FFFrameList *pNew = (FFFrameList *)av_mallocz(sizeof(FFFrameList));
	pNew->pFrame = pFrameRGB;
	pNew->buffer = pBuffer;
	pNew->ope = ope;

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

int TFFmpegDecoder::DestroyFrameQueue()
{
	ClearFrameQueue();
	TFF_CloseMutex(_pQ->mutex);
	TFF_DestroyCond(&_pQ->cond);
	free(_pQ);
	_pQ = NULL;
	return 0;
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