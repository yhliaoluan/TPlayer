#include "TFFmpegWin32.h"

TFF_Cond TFF_CreateCondWin32()
{
	TFF_Cond pcv = (TFF_Cond)malloc(sizeof(TFF_Cond_Win32));
	pcv->waitersCount = 0;
	InitializeCriticalSection(&pcv->waitersLock);
	pcv->waitersDone = TFF_CreateEvent(FALSE, FALSE);
	pcv->sema = CreateSemaphore(NULL, 0, 0x7fffffff, NULL);
	return pcv;
}

int TFF_DestroyCondWin32(TFF_Cond *cv)
{
	if(*cv)
	{
		DeleteCriticalSection(&(*cv)->waitersLock);
		CloseHandle((*cv)->sema);
		free(*cv);
		*cv = NULL;
	}
	return 0;
}

int TFF_WaitCondWin32(TFF_Cond cv, TFF_Mutex mutex)
{
	//add waiter count
	EnterCriticalSection(&cv->waitersLock);
	cv->waitersCount++;
	LeaveCriticalSection(&cv->waitersLock);

	//release mutex and wait on semaphore
	SignalObjectAndWait(mutex, cv->sema, INFINITE, FALSE);

	//awake from semaphore. check if is the last waiter
	EnterCriticalSection(&cv->waitersLock);
	cv->waitersCount--;
	int lastWaiter = cv->waitersCount == 0;
	LeaveCriticalSection(&cv->waitersLock);

	if(lastWaiter)
		SignalObjectAndWait(cv->waitersDone, mutex, INFINITE, FALSE);
	else
		WaitForSingleObject(mutex, INFINITE);
	return 0;
}

int TFF_CondBroadcastWin32(TFF_Cond cv)
{
	EnterCriticalSection(&cv->waitersLock);
	if(cv->waitersCount > 0)
	{
		ReleaseSemaphore(cv->sema, cv->waitersCount, NULL);
		LeaveCriticalSection(&cv->waitersLock);
		WaitForSingleObject(cv->waitersDone, INFINITE);
	}
	else
		LeaveCriticalSection(&cv->waitersLock);
	return 0;
}