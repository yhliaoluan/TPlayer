#include <Windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <iostream>
#include "TFFmpeg.h"

using std::cout;
using std::cin;
using std::endl;

void __stdcall NewFrame(FFFrame *p)
{
	cout << "New frame. time:" << p->time << " size:" << p->size << endl;
}
void __stdcall Finished(void)
{
	cout << "Finished." << endl;
}

int wmain(int argc, wchar_t *argv[])
{
	void *handle;
	FFSettings settings;
	char msg[MAX_PATH] = {0};
	int err = FF_Init();
	if(err >= 0)
	{
		err = FF_InitFile(argv[1], &settings, &handle);
	}

	if(err >= 0)
	{
		err = FF_SetCallback(handle, NewFrame, Finished);
	}

	if(err >= 0)
	{
		cout << "duration: " << settings.duration * settings.timebaseNum / (double)settings.timebaseDen << endl;
		cout << settings.width << "x" << settings.height << endl;
		cout << "fps: " << settings.fpsNum / (double)settings.fpsDen << endl;
		cout << settings.codecName << endl;
	}

	if(err >= 0)
	{
		cout << "input msg:" << endl;
		while(cin >> msg)
		{
			if(strcmp(msg, "-1") == 0)
			{
				break;
			}
			else if(strcmp(msg, "pause") == 0)
			{
				FF_Pause(handle);
			}
			else if(strcmp(msg, "run") == 0)
			{
				FF_Run(handle);
			}
			else if(strcmp(msg, "stop") == 0)
			{
				FF_Stop(handle);
			}
			else if(strcmp(msg, "step") == 0)
			{
				FF_ReadNextFrame(handle);
			}
		}
	}

	if(err >= 0)
	{
		err = FF_CloseHandle(handle);
	}

	if(err >= 0)
	{
		cout << "exit." << endl;
	}
	return 0;
}