#ifndef _T_FFMPEG_WIN32_H_
#define _T_FFMPEG_WIN32_H_

#include <Windows.h>

#define TFF_Thread HANDLE
#define TFF_Mutex HANDLE
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
TFF_CreateEvent()
{
	return CreateEvent(NULL, FALSE, FALSE, NULL);
}

int inline
TFF_WaitEvent(TFF_Event ev, int timeout)
{
	return WaitForSingleObject(ev, timeout);
}

#endif