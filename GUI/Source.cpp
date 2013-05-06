#include <Windows.h>

typedef struct
{
	HANDLE event;
} FF_Cond;

int FF_CondInit(FF_Cond **cv)
{
	*cv = (FF_Cond *)malloc(sizeof(FF_Cond));
	(*cv)->event = CreateEvent(NULL, FALSE, FALSE, NULL);
	return 0;
}

int FF_CondDestroy(FF_Cond **cv)
{
	CloseHandle((*cv)->event);
	free(*cv);
	*cv = NULL;
	return 0;
}

int FF_CondWait(FF_Cond *cv, HANDLE mutex)
{
	SignalObjectAndWait(mutex, cv->event, INFINITE, FALSE);
	WaitForSingleObject(mutex, INFINITE);
}

int FF_CondSignal(FF_Cond *cv)
{
	SetEvent(cv->event);
	return 0;
}

int main()
{
	return 0;
}