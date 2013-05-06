#ifndef _T_FFMPEG_WIN32_H_
#define _T_FFMPEG_WIN32_H_

#include <Windows.h>

#undef TFF_Thread
#define TFF_Thread HANDLE

#undef TFF_Mutex
#define TFF_Mutex HANDLE

#undef TFF_Event
#define TFF_Event HANDLE

#define TFF_CloseThread CloseHandle
#define TFF_CloseEvent CloseHandle
#define TFF_CloseMutex CloseHandle

#define TFF_OutputDebugString OutputDebugStringA

#define TFF_SetEvent SetEvent

#define TFF_GetMutex WaitForSingleObject

#define TFF_ReleaseMutex ReleaseMutex

#define TFF_WaitThread WaitForSingleObject
#define TFF_Sleep Sleep

#define TFF_INFINITE INFINITE

TFF_Thread inline
TFF_CreateThread(unsigned long (__stdcall *pf)(void *), void *p)
{
	return CreateThread(NULL, 0, pf, p, 0, NULL);
}

TFF_Mutex inline
TFF_CreateMutex()
{
	return CreateMutex(NULL, FALSE, NULL);
}

TFF_Event inline
TFF_CreateEvent(BOOL manualReset, BOOL initalState)
{
	return CreateEvent(NULL, manualReset, initalState, NULL);
}

int inline
TFF_WaitEvent(TFF_Event ev, int timeout)
{
	return WaitForSingleObject(ev, timeout);
}

typedef struct
{
	size_t waitersCount;
	CRITICAL_SECTION waitersLock;
	TFF_Event waitersDone;
	HANDLE sema;
} TFF_Cond_Win32;
#define TFF_Cond TFF_Cond_Win32*

TFF_Cond TFF_CreateCondWin32()
{
	TFF_Cond pcv = (TFF_Cond)malloc(sizeof(TFF_Cond_Win32));
	pcv->waitersCount = 0;
	InitializeCriticalSection(&pcv->waitersLock);
	pcv->waitersDone = TFF_CreateEvent(FALSE, FALSE);
	pcv->sema = CreateSemaphore(NULL, 0, 0x7fffffff, NULL);
	return pcv;
}
#define TFF_CreateCond TFF_CreateCondWin32

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
#define TFF_DestroyCond TFF_DestroyCondWin32

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
#define TFF_WaitCond TFF_WaitCondWin32

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
#define TFF_CondBroadcast TFF_CondBroadcastWin32

#endif