#ifndef _TFF_LOG_H_
#define _TFF_LOG_H_

enum TFFLogLevel
{
	TFF_LOG_LEVEL_DEBUG,
	TFF_LOG_LEVEL_INFO,
	TFF_LOG_LEVEL_WARNING,
	TFF_LOG_LEVEL_ERROR
};

int __stdcall TFFLogInit(const char *file);
void __stdcall TFFLog(TFFLogLevel level, const char *utf8, ...);
#endif