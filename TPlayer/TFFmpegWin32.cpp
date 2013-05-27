#include "TFFmpegWin32.h"

TFF_Cond TFF_CreateCondWin32()
{
	TFF_Cond pcv = (TFF_Cond)malloc(sizeof(TFF_Cond_Win32));
	pcv->waiters = 0;
	pcv->signals = 0;
	InitializeCriticalSection(&pcv->mutex);
	pcv->semaWait = CreateSemaphore(NULL, 0, 0x7fffffff, NULL);
	pcv->semaDone = CreateSemaphore(NULL, 0, 0x7fffffff, NULL);
	return pcv;
}

int TFF_DestroyCondWin32(TFF_Cond *cv)
{
	if(*cv)
	{
		DeleteCriticalSection(&(*cv)->mutex);
		CloseHandle((*cv)->semaWait);
		CloseHandle((*cv)->semaDone);
		free(*cv);
		*cv = NULL;
	}
	return 0;
}

int TFF_WaitCondWin32(TFF_Cond cv, TFF_Mutex mutex)
{
	return TFF_WaitCondTimeoutWin32(cv, mutex, TFF_INFINITE);
}

int TFF_WaitCondTimeoutWin32(TFF_Cond cv, TFF_Mutex mutex, unsigned long timeout)
{
	DWORD ret;
	//add waiter count
	EnterCriticalSection(&cv->mutex);
	cv->waiters++;
	LeaveCriticalSection(&cv->mutex);

	//release mutex and wait on semaphore
	if(timeout == TFF_INFINITE)
		ret = SignalObjectAndWait(mutex, cv->semaWait, INFINITE, FALSE);
	else
		ret = SignalObjectAndWait(mutex, cv->semaWait, timeout, FALSE);

	EnterCriticalSection(&cv->mutex);
	if(cv->signals > 0)
	{
		//if there is a signal and the thread is awake because of timeout
		//we need eat a conditional signal
		if(ret > 0)
			WaitForSingleObject(cv->semaWait, INFINITE);
		cv->signals--;
	}
	cv->waiters--;
	LeaveCriticalSection(&cv->mutex);

	SignalObjectAndWait(cv->semaDone, mutex, INFINITE, FALSE);
	return ret;
}

int TFF_BroadcastCondWin32(TFF_Cond cv)
{
	EnterCriticalSection(&cv->mutex);
	if(cv->waiters > cv->signals)
	{
		int num = cv->waiters - cv->signals;
		cv->signals = cv->waiters;
		ReleaseSemaphore(cv->semaWait, num, NULL);
		LeaveCriticalSection(&cv->mutex);
		for(int i = 0; i < num; i++)
			WaitForSingleObject(cv->semaDone, INFINITE);
	}
	else
		LeaveCriticalSection(&cv->mutex);
	return 0;
}