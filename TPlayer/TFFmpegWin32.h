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

typedef struct _st_TFF_Cond_Win32
{
	size_t waitersCount;
	CRITICAL_SECTION waitersLock;
	TFF_Event waitersDone;
	HANDLE sema;
} TFF_Cond_Win32, *PTFF_Cond_Win32;
#define TFF_Cond PTFF_Cond_Win32

TFF_Cond TFF_CreateCondWin32();
#define TFF_CreateCond TFF_CreateCondWin32

int TFF_DestroyCondWin32(TFF_Cond *cv);
#define TFF_DestroyCond TFF_DestroyCondWin32

int TFF_WaitCondWin32(TFF_Cond cv, TFF_Mutex mutex);
#define TFF_WaitCond TFF_WaitCondWin32

int TFF_CondBroadcastWin32(TFF_Cond cv);
#define TFF_CondBroadcast TFF_CondBroadcastWin32

#endif