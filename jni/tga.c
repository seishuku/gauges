/*
Copyright 2012 Matt Williams
Targa image loader, modified for Android
*/

#include <stdio.h>
#include <malloc.h>
#include <string.h>
#include <android/asset_manager.h>
#include <android/log.h>
#include "image.h"

#define LOGI(...) ((void)__android_log_print(ANDROID_LOG_INFO, "native-activity", __VA_ARGS__))

#ifndef FREE
#define FREE(p) { if(p) { free(p); p=NULL; } }
#endif

extern AAssetManager *assetManager;

void rle_read(unsigned char *row, int width, int bpp, AAsset *stream)
{
	int pos=0, len, i;
	unsigned char header;

	while(pos<width)
	{
		AAsset_read(stream, &header, 1);

		len=(header&0x7F)+1;

		if(header&0x80)
		{
			unsigned long buffer;

			AAsset_read(stream, &buffer, bpp);

			for(i=0;i<len*bpp;i+=bpp)
				memcpy(&row[bpp*pos+i], &buffer, bpp);
		}
		else
			AAsset_read(stream, &row[bpp*pos], len*bpp);

		pos+=len;
	}
}

int rle_type(unsigned char *data, unsigned short pos, unsigned short width, unsigned char bpp)
{
	if(!memcmp(data+bpp*pos, data+bpp*(pos+1), bpp))
	{
		if(!memcmp(data+bpp*(pos+1), data+bpp*(pos+2), bpp))
			return 1;
	}

	return 0;
}

int TGA_Load(char *Filename, Image_t *Image)
{
	AAsset *stream=NULL;
	unsigned char *ptr;
	unsigned char IDLength;
	unsigned char ColorMapType, ImageType;
	unsigned short ColorMapStart, ColorMapLength;
	unsigned char ColorMapDepth;
	unsigned short XOffset, YOffset;
	unsigned short Width, Height;
	unsigned char Depth;
	unsigned char ImageDescriptor;
	int i, bpp;

	if((stream=AAssetManager_open(assetManager, Filename, AASSET_MODE_BUFFER))==NULL)
	{
		LOGI("%s AAssetManager_open failed.\n", Filename);
		return 0;
	}

	AAsset_read(stream, &IDLength, 1);
	AAsset_read(stream, &ColorMapType, 1);
	AAsset_read(stream, &ImageType, 1);
	AAsset_read(stream, &ColorMapStart, 2);
	AAsset_read(stream, &ColorMapLength, 2);
	AAsset_read(stream, &ColorMapDepth, 1);
	AAsset_read(stream, &XOffset, 2);
	AAsset_read(stream, &YOffset, 2);
	AAsset_read(stream, &Width, 2);
	AAsset_read(stream, &Height, 2);
	AAsset_read(stream, &Depth, 1);
	AAsset_read(stream, &ImageDescriptor, 1);
	AAsset_seek(stream, IDLength, SEEK_CUR);

	switch(ImageType)
	{
		case 11:
		case 10:
		case 3:
		case 2:
			break;

		default:
			LOGI("ImageType kicked out %d\n", ImageType);
			AAsset_close(stream);
			return 0;
	}

	switch(Depth)
	{
		case 32:
		case 24:
		case 16:
		case 8:
			bpp=Depth>>3;

			Image->Data=(unsigned char *)malloc(Width*Height*bpp);

			if(Image->Data==NULL)
				return 0;

			if(ImageType==10||ImageType==11)
			{
				for(i=0, ptr=(unsigned char *)Image->Data;i<Height;i++, ptr+=Width*bpp)
					rle_read(ptr, Width, bpp, stream);
			}
			else
				AAsset_read(stream, Image->Data, Width*Height*bpp);
			break;

		default:
			LOGI("Depth kicked out %d\n", Depth);
			AAsset_close(stream);
			return 0;
	}

	AAsset_close(stream);

	if(ImageDescriptor&0x20)
	{
		int Scanline=Width*bpp, Size=Scanline*Height;
		unsigned char *Buffer=(unsigned char *)malloc(Size);

		if(Buffer==NULL)
		{
			FREE(Image->Data);
			return 0;
		}

		for(i=0;i<Height;i++)
			memcpy(Buffer+(Size-(i+1)*Scanline), Image->Data+i*Scanline, Scanline);

		memcpy(Image->Data, Buffer, Size);

		FREE(Buffer);
	}

	Image->Width=Width;
	Image->Height=Height;
	Image->Depth=Depth;

	return 1;
}
