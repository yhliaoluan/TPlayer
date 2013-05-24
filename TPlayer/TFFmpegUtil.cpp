#include <stdio.h>
#include "TFFmpegUtil.h"

void
DebugOutput(const char *msg, ...)
{
#ifdef FF_DEBUG_OUTPUT
	char m[1024] = {0};
	va_list vl;
	va_start(vl, msg);
	vsprintf_s(m, msg, vl);
	TFF_OutputDebugString(m);
	//vprintf(msg, vl);
	va_end(vl);
#endif
}