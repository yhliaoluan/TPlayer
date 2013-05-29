#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#include <share.h>
#include <time.h>
#include <Windows.h> //TODO: cross-platform
#include "log.h"

static FILE *_f;
static char _file[260] = {0};
static char _msg[1024] = {0};
static char _fmt[960] = {0};

int __stdcall TFFLogInit(const char *file)
{
	if(_f)
		return 0;
	strcpy_s(_file, 260, file);
	_f = _fsopen(_file, "a", _SH_DENYWR);
	if(_f)
		return 0;
	return -1;
}

static inline char *GetLevelStr(TFFLogLevel level)
{
	switch(level)
	{
	case TFF_LOG_LEVEL_DEBUG:
		return "D";
	case TFF_LOG_LEVEL_INFO:
		return "I";
	case TFF_LOG_LEVEL_WARNING:
		return "WARNING";
	case TFF_LOG_LEVEL_ERROR:
		return "ERROR";
	default:
		return "U";
	}
}

void __stdcall TFFLog(TFFLogLevel level, const char *utf8, ...)
{
	va_list vl;
	SYSTEMTIME t;
	va_start(vl, utf8);
	vsprintf_s(_fmt, utf8, vl);
	GetLocalTime(&t);
	sprintf_s(_msg, "%d-%d-%d %d:%d:%d.%d [%s] - %s", t.wYear, t.wMonth, t.wDay, t.wHour, t.wMinute,
		t.wSecond, t.wMilliseconds, GetLevelStr(level), _fmt);
	fwrite(_msg, 1, strnlen_s(_msg, 1024), _f);
	fwrite("\n", 1, 1, _f);
	fflush(_f);
	va_end(vl);
}