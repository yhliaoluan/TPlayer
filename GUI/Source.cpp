#include <Windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <iostream>
#include "TFFmpeg.h"
#include "SDL.h"

extern "C"
{
#include <libavformat\avformat.h>
#include <libswscale\swscale.h>
}

using std::cout;
using std::cin;
using std::endl;

#define CONTROL_HEIGHT 100
#define SDL_AUDIO_BUFFER_SIZE 1024

#define AUDIO_INBUF_SIZE 20480
#define AUDIO_REFILL_THRESH 4096
#define AVCODEC_MAX_AUDIO_FRAME_SIZE 409600

typedef struct _st_PlayContext
{
	SDL_Surface *window;
	SDL_Overlay *overlay;
} SDLPlayerContext;

static SDLPlayerContext *_context;

static int _beginTime;

void __stdcall NewFrame(FFFrame *p)
{
	SDL_Rect rect;

	//if(!_context->overlay)
	//	_context->overlay = SDL_CreateYUVOverlay(p->width, p->height, SDL_IYUV_OVERLAY, _context->window);

	//if(_context->overlay->w != p->width ||
	//	_context->overlay->h != p->height)
	//{
	//	SDL_FreeYUVOverlay(_context->overlay);
	//	_context->overlay = SDL_CreateYUVOverlay(p->width, p->height, SDL_IYUV_OVERLAY, _context->window);
	//}

	int elMS = SDL_GetTicks() - _beginTime;
	int showMS = (int)(p->time * 1000);
	if(showMS > elMS)
	{
		SDL_Delay(showMS - elMS);
	}

	int err = SDL_LockYUVOverlay(_context->overlay);

	memcpy(_context->overlay->pixels[0], p->data[0], p->linesize[0] * p->height);
	memcpy(_context->overlay->pixels[1], p->data[1], p->linesize[1] * p->height >> 1);
	memcpy(_context->overlay->pixels[2], p->data[2], p->linesize[2] * p->height >> 1);

	SDL_UnlockYUVOverlay(_context->overlay);
	rect.x = 0;
	rect.y = 0;
	rect.w = p->width;
	rect.h = p->height;
	SDL_DisplayYUVOverlay(_context->overlay, &rect);
}

void __stdcall Finished(void)
{
	cout << "Finished." << endl;
}

void AudioCallback(void *userdata, Uint8 *stream, int len)
{
}

static int InitSDL(FFSettings *setting)
{
	int ret = 0;
	ret = SDL_Init(SDL_INIT_EVERYTHING);

	if(ret >= 0)
	{
		_context = (SDLPlayerContext *)malloc(sizeof(SDLPlayerContext));
		memset(_context, 0, sizeof(SDLPlayerContext));
		_context->window = SDL_SetVideoMode(setting->width, setting->height + CONTROL_HEIGHT, 0, SDL_SWSURFACE | SDL_RESIZABLE);

		_context->overlay = SDL_CreateYUVOverlay(setting->width, setting->height, SDL_IYUV_OVERLAY, _context->window);
		SDL_WM_SetCaption("SDL Player", NULL);
	}

	if(ret >= 0)
	{
		SDL_AudioSpec wantedSpec;
		wantedSpec.freq = setting->audioSampleRate;
		wantedSpec.format = AUDIO_S16SYS;
		wantedSpec.channels = setting->audioChannels;
		wantedSpec.silence = 0;
		wantedSpec.samples = SDL_AUDIO_BUFFER_SIZE;
		wantedSpec.callback = AudioCallback;

		SDL_AudioSpec spec;
		SDL_OpenAudio(&wantedSpec, &spec);
	}

	return 0;
}

static void LoopEvents(FFSettings *pSettings, void *handle)
{
	BOOL running = TRUE;

	SDL_Event event;
	int w, h;
	double r;
	double srcR = pSettings->width / (double)pSettings->height;
	while(running)
	{
		SDL_Delay(50);
		while(SDL_PollEvent(&event))
		{
			switch(event.type)
			{
			case SDL_QUIT:
				running = FALSE;
				break;
			case SDL_VIDEORESIZE:
				//w = event.resize.w;
				//h = event.resize.h;
				//r = w / (double)h;
				//if(r > srcR)
				//	w = (int)(h * srcR + 0.5);
				//else if(r < srcR)
				//	h = (int)(w / srcR + 0.5);
				//if(w % 2 != 0)
				//	w -= 1;
				//if(h % 2 != 0)
				//	h -= 1;
				//cout << "Resize w:" << w << " h:" << h << endl;
				//FF_SetResolution(handle, w, h);
				break;
			default:
				break;
			}
		}
	}
}

int player(int argc, wchar_t *argv[])
{
	void *handle;
	FFSettings settings;
	char msg[MAX_PATH] = {0};
	int err = FF_Init();
	if(err >= 0)
	{
		FFInitSetting initSetting = {0};
		wcscpy_s(initSetting.fileName, argv[1]);
		initSetting.dstFramePixFmt = FF_FRAME_PIXFORMAT_YUV420;
		err = FF_InitFile(&initSetting, &settings, &handle);
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
		InitSDL(&settings);
	}

	_beginTime = SDL_GetTicks();
	FF_Run(handle);

	LoopEvents(&settings, handle);

	if(err >= 0)
	{
		err = FF_CloseHandle(handle);

		if(_context)
		{
			if(_context->window)
				SDL_FreeSurface(_context->window);
			if(_context->overlay)
				SDL_FreeYUVOverlay(_context->overlay);

			free(_context);
			_context = NULL;
		}

		SDL_Quit();
	}

	if(err >= 0)
	{
		cout << "exit." << endl;
	}
	return 0;
}

int test(wchar_t *file)
{
	char szFile[1024] = {0};
	AVFormatContext *fmtCtx = NULL;
	int ret = 0;
	AVPacket *pkt = NULL;
	int videoIndex, audioIndex, subTitleIndex;
	av_register_all();

	WideCharToMultiByte(
		CP_UTF8,
		0,
		file,
		-1,
		szFile,
		1024,
		NULL,
		NULL);

	ret = avformat_open_input(&fmtCtx, szFile, NULL, NULL);
	ret = avformat_find_stream_info(fmtCtx, NULL);

	for(unsigned int i = 0; i < fmtCtx->nb_streams; i++)
	{
		if(fmtCtx->streams[i]->codec->codec_type == AVMEDIA_TYPE_VIDEO)
			videoIndex = i;
		else if(fmtCtx->streams[i]->codec->codec_type == AVMEDIA_TYPE_AUDIO)
			audioIndex = i;
		else if(fmtCtx->streams[i]->codec->codec_type == AVMEDIA_TYPE_SUBTITLE)
			subTitleIndex = i;
		else
			cout << "Unknown codec type " << (int)fmtCtx->streams[i]->codec->codec_type << endl;
	}

	cout << "Video index:" << videoIndex << endl;
	cout << "Audio index:" << audioIndex << endl;
	cout << "Subtitle index:" << subTitleIndex << endl;

	AVPacket *pPkt = (AVPacket *)av_mallocz(sizeof(AVPacket));
	av_init_packet(pPkt);
	char cmd[MAX_PATH] = {0};
	while(cin >> cmd)
	{
		if(strcmp(cmd, "q") == 0)
			break;
		if(av_read_frame(fmtCtx, pPkt) < 0)
			break;
		if(pPkt->stream_index == videoIndex)
			cout << "Packt video";
		else if(pPkt->stream_index == audioIndex)
			cout << "Packt audio";
		else if(pPkt->stream_index == subTitleIndex)
			cout << "Packt subtitle";
		else
			cout << "Packet index:" << pPkt->stream_index;
		cout << " DTS:" << pPkt->dts;
		cout << " Duration:" << pPkt->duration;
		cout << " PTS:" << pPkt->pts;
		cout << " Position:" << pPkt->pos;
		cout << " Size:" << pPkt->size;
		cout << endl;
	}

	av_free_packet(pPkt);
	av_freep(&pPkt);
	avformat_close_input(&fmtCtx);
	return 0;
}

void test_play_sound_callback(void *userdata, uint8_t *stream, int len)
{
	cout << "test_play_sound_callback ";
	cout << "len " << len << endl;
	AVFormatContext *fmtCtx = (AVFormatContext *)userdata;
	AVFrame *frame = avcodec_alloc_frame();
	AVPacket pkt;
	int gotFrame = -1;

	av_init_packet(&pkt);
	while(av_read_frame(fmtCtx, &pkt))
	{
		if(pkt.stream_index == 1)
		{
			break;
		}
	}

	int decSize = avcodec_decode_audio4(fmtCtx->streams[1]->codec,
					frame,
					&gotFrame,
					&pkt);
}

void test_play_sound(wchar_t *file)
{
	char szFile[1024] = {0};
	AVFormatContext *fmtCtx = NULL;
	int ret = 0;
	AVPacket *pkt = NULL;
	int audioIndex;
	av_register_all();

	WideCharToMultiByte(
		CP_UTF8,
		0,
		file,
		-1,
		szFile,
		1024,
		NULL,
		NULL);

	ret = avformat_open_input(&fmtCtx, szFile, NULL, NULL);
	ret = avformat_find_stream_info(fmtCtx, NULL);

	for(unsigned int i = 0; i < fmtCtx->nb_streams; i++)
	{
		if(fmtCtx->streams[i]->codec->codec_type == AVMEDIA_TYPE_AUDIO)
		{
			audioIndex = i;
			break;
		}
	}

	SDL_Init(SDL_INIT_EVERYTHING);

	AVStream *as = fmtCtx->streams[audioIndex];
	AVCodec *codec = avcodec_find_decoder(as->codec->codec_id);
	ret = avcodec_open2(as->codec, codec, NULL);

	SDL_AudioSpec wantedSpec;
	wantedSpec.freq = as->codec->sample_rate;
	wantedSpec.format = AUDIO_S16SYS;
	wantedSpec.channels = as->codec->channels;
	wantedSpec.silence = 0;
	wantedSpec.samples = 1024;
	wantedSpec.callback = test_play_sound_callback;
	wantedSpec.userdata = fmtCtx;

	SDL_AudioSpec spec;
	ret = SDL_OpenAudio(&wantedSpec, &spec);

	if(ret < 0)
	{
		cout << "SDL_OpenAudio err " << ret << endl;
	}

	SDL_PauseAudio(0);

	char msg[MAX_PATH] = {0};
	while(cin >> msg)
	{
		if(strcmp(msg, "q") == 0)
			break;
	}

	cout << "quit" << endl;
	SDL_CloseAudio();
	SDL_Quit();

	avcodec_close(as->codec);
	avformat_close_input(&fmtCtx);
}

static void print_frame(const AVFrame *frame, FILE *output)
{
    const int n = frame->nb_samples * av_get_channel_layout_nb_channels(av_frame_get_channel_layout(frame));
    const uint16_t *p     = (uint16_t*)frame->data[0];
    const uint16_t *p_end = p + n;
 
    while (p < p_end) {
        fputc(*p    & 0xff, output);
        fputc(*p>>8 & 0xff, output);
        p++;
    }
    fflush(output);
}

int sample()
{
	const char *outfilename = "D:\\test.pcm";
	const char *filename = "D:\\Music\\Avril Lavigne - Innocence.mp3";
	AVFormatContext *pFormatCtx = NULL;
	AVCodec *codec;
	AVFrame *frame;
	AVCodecContext *c = NULL;//编码格式
	int out_size, len, audioStream, i, out_size2, buffer_size;
	FILE *f, *outfile, *outfile2;
	uint8_t *outbuf;
	AVPacket avpkt;
	/* register all the codecs */
	avcodec_register_all();/*注册所有的编码解码器*/
	av_register_all();// //注册所有可解码类型
	av_init_packet(&avpkt);
    
	printf("Audio decoding\n");
    
	if(avformat_open_input(&pFormatCtx, filename, NULL, NULL) != 0)//打开要编码文件，将音频流信息读入输入容器（获取文件格式）
		{
				printf("open %s failed\n" , filename);
				return -1;
		}
    
		if(av_find_stream_info(pFormatCtx) < 0)//获取文件内音视频流的信息
		{
				printf("find stream info failed\n");
				return -1;
		}     
          
		av_dump_format(pFormatCtx, 0, filename, 0);
    
		audioStream = -1;
		for(i=0; i<pFormatCtx->nb_streams; i++)
		{

			if(pFormatCtx->streams[i]->codec->codec_type == AVMEDIA_TYPE_AUDIO) {//从音视频流中循环查找媒体类型为音频的流
				audioStream = i;
				break;
			}
		} 

		if(audioStream == -1)//这里表示未找到音频流
		{
				printf("find audioStream failed\n");
				return -1;
		}
	c = pFormatCtx->streams[audioStream]->codec;//将找到音频流的编码格式赋值给c

		/* find the mpeg audio decoder */
		codec = avcodec_find_decoder(c->codec_id);//根据获取的编码格式查找对应的解码器
		if (!codec) {
			printf("codec not found\n");
			return -1;
		}

		/* open it */
		if (avcodec_open2(c, codec, NULL) < 0) {//打开解码器
			printf("could not open codec\n");
			return -1;
		}

		outbuf = (uint8_t *)malloc(AVCODEC_MAX_AUDIO_FRAME_SIZE);

		outfile = fopen(outfilename, "wb");
		if (!outfile) {
			printf("open outfile failed\n");
			av_free(c);
			return -1;
		}

		while (av_read_frame(pFormatCtx, &avpkt) >= 0) {//zch 这边按帧读取音频流
			out_size = AVCODEC_MAX_AUDIO_FRAME_SIZE;// 将输出大小设置为最大的音频帧大小
                   
			len = avcodec_decode_audio3(c, (short *)outbuf, &out_size, &avpkt);//zch 解码当前音频流到outbuf
			//len = avcodec_decode_audio4(c, frame, &out_size, &avpkt);
			 printf("Audio decoding %d\n", out_size);
			if (len < 0) {
				printf("Error while decoding\n");
				continue;
			}
			if (out_size > 0) {
				/* if a frame has been decoded, output it */
				//fwrite(frame->data[0], 1, data_size, outfile);// 将解码后的数据写入输出文件中 
				fwrite(outbuf, 1, out_size, outfile);

				//if(out_size > 4096)
				//{
				//	fflush(outfile);
				//	fwrite(outbuf+4096, 1, out_size-4096, outfile);
				//	//fwrite(frame->data[0]+4096, 1, data_size-4096, outfile);
				//}
			}
	}
	return 0;
}

int save_sound4(char *srcFile, char *dstFile)
{
	AVFormatContext *fmtCtx = NULL;
	int ret = 0;
	int audioIndex;
	FILE *dst = NULL;
	av_register_all();

	ret = avformat_open_input(&fmtCtx, srcFile, NULL, NULL);
	ret = avformat_find_stream_info(fmtCtx, NULL);

	av_dump_format(fmtCtx, 0, srcFile, 0);
	for(unsigned int i = 0; i < fmtCtx->nb_streams; i++)
	{
		if(fmtCtx->streams[i]->codec->codec_type == AVMEDIA_TYPE_AUDIO)
		{
			audioIndex = i;
			cout << "Audio index:" << i << endl;
			break;
		}
	}

	AVStream *as = fmtCtx->streams[audioIndex];
	AVCodec *codec = avcodec_find_decoder(as->codec->codec_id);
	ret = avcodec_open2(as->codec, codec, NULL);

	dst = fopen(dstFile, "wb");

	AVFrame *frame = avcodec_alloc_frame();
	AVPacket packet;
	av_init_packet(&packet);
	while (av_read_frame(fmtCtx, &packet) >= 0)
	{
		if(packet.stream_index == audioIndex)
		{
			int got = 0;
			avcodec_get_frame_defaults(frame);
			int len = avcodec_decode_audio4(as->codec,
				frame,
				&got,
				&packet);
			if(len != packet.size)
				cout << "Not equal." << endl;
			if(!got)
				cout << "Not got." << endl;
			print_frame(frame, dst);
		}
		av_free_packet(&packet);
	}
	fclose(dst);
	avcodec_close(as->codec);
	avformat_close_input(&fmtCtx);
	return 0;
}

int save_sound_file(char *srcFile, char *dstFile)
{
	AVFormatContext *fmtCtx = avformat_alloc_context();
	int ret = 0;
	int audioIndex;
	FILE *dst = NULL;
	av_register_all();

	ret = avformat_open_input(&fmtCtx, srcFile, NULL, NULL);
	ret = avformat_find_stream_info(fmtCtx, NULL);

	av_dump_format(fmtCtx, 0, srcFile, 0);
	for(unsigned int i = 0; i < fmtCtx->nb_streams; i++)
	{
		if(fmtCtx->streams[i]->codec->codec_type == AVMEDIA_TYPE_AUDIO)
		{
			audioIndex = i;
			cout << "Audio index:" << i << endl;
			break;
		}
	}

	AVStream *as = fmtCtx->streams[audioIndex];
	AVCodec *codec = avcodec_find_decoder(as->codec->codec_id);

	if (codec->capabilities & CODEC_CAP_TRUNCATED)  
    {  
		as->codec->flags |= CODEC_CAP_TRUNCATED;  
    }  

	ret = avcodec_open2(as->codec, codec, NULL);

	dst = fopen(dstFile, "wb");

	AVPacket packet;
	av_init_packet(&packet);
	int out_size = 19200 * 100;
	uint8_t * inbuf = (uint8_t *)av_malloc(out_size);
	while (av_read_frame(fmtCtx, &packet) >= 0)
	{
		if(packet.stream_index == audioIndex)
		{
			out_size = 19200 * 100;
			int len = avcodec_decode_audio3(as->codec,
				(int16_t *)inbuf,
				&out_size,
				&packet);
			if (len<0)
			{
				printf("Error while decoding.\n");
			}
			if(out_size>0)
			{
				fwrite(inbuf, 1, out_size, dst);
			}
			//if(out_size > 4096)
			//{

			//	fflush(dst);
			//	fwrite(inbuf+4096, 1, out_size-4096, dst);
			//	//fwrite(frame->data[0]+4096, 1, data_size-4096, outfile);
			//}	

		}
		av_free_packet(&packet);
	}
	fclose(dst);
	av_free(inbuf);
	avcodec_close(as->codec);
	avformat_close_input(&fmtCtx);
	return 0;
}

void play_sound_cb1(void *userdata, uint8_t *stream, int len)
{
	FILE *file = (FILE *)userdata;
	fread(stream, 1, len, file);
}

void play_sound_from_file(char *file)
{
	SDL_Init(SDL_INIT_EVERYTHING);

	FILE *f = fopen(file, "rb");

	SDL_AudioSpec wantedSpec;
	wantedSpec.freq = 44100;
	wantedSpec.format = AUDIO_S16SYS;
	wantedSpec.channels = 2;
	wantedSpec.silence = 0;
	wantedSpec.samples = 1024;
	wantedSpec.callback = play_sound_cb1;
	wantedSpec.userdata = f;

	SDL_AudioSpec spec;
	int ret = SDL_OpenAudio(&wantedSpec, &spec);

	SDL_PauseAudio(0);

	char cmd[30] = {0};
	while(cin >> cmd)
	{
		if(strcmp(cmd, "q") == 0)
			break;
	}

	SDL_CloseAudio();
	fclose(f);
}

int decode(const char* input_filename)
{
  av_register_all();

  AVFormatContext* container=avformat_alloc_context();
  if(avformat_open_input(&container,input_filename,NULL,NULL)<0){
    printf("Could not open file");
  }

  if(avformat_find_stream_info(container, NULL)<0){
      printf("Could not find file info");
  }
  av_dump_format(container,0,input_filename,false);

  int stream_id=-1;
  int i;
  for(i=0;i<container->nb_streams;i++){
    if(container->streams[i]->codec->codec_type==AVMEDIA_TYPE_AUDIO){
        stream_id=i;
        break;
    }
  }
  if(stream_id==-1){
    printf("Could not find Audio Stream");
  }

  AVDictionary *metadata=container->metadata;
  AVCodecContext *ctx=container->streams[stream_id]->codec;
  AVCodec *codec=avcodec_find_decoder(ctx->codec_id);

  if(codec==NULL){
    printf("cannot find codec!");
  }

  if(avcodec_open2(ctx,codec,NULL)<0){
     printf("Codec cannot be found");
  }

  AVSampleFormat sfmt = ctx->sample_fmt;

  AVPacket packet;
  av_init_packet(&packet);
  AVFrame *frame = avcodec_alloc_frame();

  int buffer_size = AVCODEC_MAX_AUDIO_FRAME_SIZE+ FF_INPUT_BUFFER_PADDING_SIZE;
  uint8_t *buffer = (uint8_t *)av_mallocz(buffer_size);
  packet.data=buffer;
  packet.size =buffer_size;

  FILE *outfile = fopen("D:\\test.pcm", "wb");

  int len;
  int frameFinished=0;

  while(av_read_frame(container,&packet) >= 0)
  {
      if(packet.stream_index==stream_id)
      {
        //printf("Audio Frame read \n");
        int len=avcodec_decode_audio4(ctx, frame, &frameFinished, &packet);

        if(frameFinished)
        {       
          if (sfmt==AV_SAMPLE_FMT_S16P)
          { // Audacity: 16bit PCM little endian stereo
            int16_t* ptr_l = (int16_t*)frame->extended_data[0];
            int16_t* ptr_r = (int16_t*)frame->extended_data[1];
            for (int i=0; i<frame->nb_samples; i++)
            {
              fwrite(ptr_l++, sizeof(int16_t), 1, outfile);
              fwrite(ptr_r++, sizeof(int16_t), 1, outfile);
            }
          }
          else if (sfmt==AV_SAMPLE_FMT_FLTP)
          { //Audacity: big endian 32bit stereo start offset 7 (but has noise)
            float* ptr_l = (float*)frame->extended_data[0];
            float* ptr_r = (float*)frame->extended_data[1];
            for (int i=0; i<frame->nb_samples; i++)
            {
                fwrite(ptr_l++, sizeof(float), 1, outfile);
                fwrite(ptr_r++, sizeof(float), 1, outfile);
             }
           }            
        }
    }
}
fclose(outfile);
av_close_input_file(container);
return 0;  
}

int wmain(int argc, wchar_t *argv[])
{
	//test_play_sound(argv[1]);
	//test(argv[1]);
	//player(argc, argv);
	save_sound_file("D:\\output.pcm", "D:\\test.pcm");
	//decode("D:\\Movies\\CC_2011_Christmas_Party_3.wmv");
	//play_sound_from_file("D:\\test.pcm");
	//sample();
	return 0;
}