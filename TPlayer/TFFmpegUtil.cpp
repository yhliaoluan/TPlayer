#include <stdio.h>
#include "TFFmpegUtil.h"

//TODO: add log util
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
	//printf("\n");
	va_end(vl);
#endif
}