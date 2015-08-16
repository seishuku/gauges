/*
Copyright 2012 Matt Williams
Various image loading and processing fuctions
*/

#include <GLES2/gl2.h>
#include <GLES2/gl2ext.h>
#include <malloc.h>
#include <stdio.h>
#include <string.h>
#include <android/log.h>
#include "_math.h"
#include "image.h"

#define LOGI(...) ((void)__android_log_print(ANDROID_LOG_INFO, "native-activity", __VA_ARGS__))

#ifndef FREE
#define FREE(p) { if(p) { free(p); p=NULL; } }
#endif

#ifndef min
#define min(a, b) ((a)<(b)?(a):(b))
#endif

#ifndef max
#define max(a, b) ((a)>(b)?(a):(b))
#endif

void _BGRtoRGB(Image_t *Image)
{
	int i;
	unsigned short temp;

	if(!((Image->Depth==32)||(Image->Depth==24)||(Image->Depth==16)))
		return;

	if(!Image->Data)
		return;

	for(i=0;i<Image->Width*Image->Height;i++)
	{
		switch(Image->Depth)
		{
			case 32:
				temp=((unsigned char *)Image->Data)[4*i+0];
				((unsigned char *)Image->Data)[4*i+0]=((unsigned char *)Image->Data)[4*i+2];
				((unsigned char *)Image->Data)[4*i+2]=temp;
				break;

			case 24:
				temp=((unsigned char *)Image->Data)[3*i+0];
				((unsigned char *)Image->Data)[3*i+0]=((unsigned char *)Image->Data)[3*i+2];
				((unsigned char *)Image->Data)[3*i+2]=temp;
				break;

			case 16:
				temp=((unsigned short *)Image->Data)[i];
				((unsigned short *)Image->Data)[i]=(short)(((temp&0x1F)<<11)|(temp&0x7E0)|((temp&0xF800)>>11));
				break;
		}
	}
}

void _MakeNormalMap(Image_t *Image)
{
	int x, y, xx, yy;
	int Channels=Image->Depth>>3;
	unsigned short *Buffer=NULL;
	const float OneOver255=1.0f/255.0f;
	float KernelX[9]={ 1.0f, 0.0f, -1.0f, 2.0f, 0.0f, -2.0f, 1.0f, 0.0f, -1.0f };
	float KernelY[9]={ -1.0f, -2.0f, -1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 2.0f, 1.0f };

	if(!((Image->Depth==32)||(Image->Depth==24)||(Image->Depth==8)))
		return;

	Buffer=(unsigned short *)malloc(sizeof(unsigned short)*Image->Width*Image->Height*4);

	if(Buffer==NULL)
		return;

	for(y=0;y<Image->Height;y++)
	{
		for(x=0;x<Image->Width;x++)
		{
			float n[3]={ 0.0f, 0.0f, 1.0f }, mag;

			for(yy=0;yy<3;yy++)
			{
				int oy=min(Image->Height-1, y+yy);

				for(xx=0;xx<3;xx++)
				{
					int ox=min(Image->Width-1, x+xx);

					n[0]+=KernelX[yy*3+xx]*(float)(Image->Data[Channels*(oy*Image->Width+ox)]*OneOver255);
					n[1]+=KernelY[yy*3+xx]*(float)(Image->Data[Channels*(oy*Image->Width+ox)]*OneOver255);
				}
			}

			mag=1.0f/sqrtf(n[0]*n[0]+n[1]*n[1]+n[2]*n[2]);
			n[0]*=mag;
			n[1]*=mag;
			n[2]*=mag;

			Buffer[4*(y*Image->Width+x)+0]=(unsigned short)(65535.0f*(0.5f*n[0]+0.5f));
			Buffer[4*(y*Image->Width+x)+1]=(unsigned short)(65535.0f*(0.5f*n[1]+0.5f));
			Buffer[4*(y*Image->Width+x)+2]=(unsigned short)(65535.0f*(0.5f*n[2]+0.5f));

			if(Channels==4)
				Buffer[4*(y*Image->Width+x)+3]=(unsigned short)(Image->Data[4*(y*Image->Width+x)+3]<<8);
			else
				Buffer[4*(y*Image->Width+x)+3]=65535;
		}
	}

	Image->Depth=64;

	FREE(Image->Data);
	Image->Data=(unsigned char *)Buffer;
}

void _Normalize(Image_t *Image)
{
	int i, Channels=Image->Depth>>3;
	unsigned short *Buffer=NULL;
	const float OneOver255=1.0f/255.0f;

	if(!((Image->Depth==32)||(Image->Depth==24)))
		return;

	Buffer=(unsigned short *)malloc(sizeof(unsigned short)*Image->Width*Image->Height*4);

	if(Buffer==NULL)
		return;

	for(i=0;i<Image->Width*Image->Height;i++)
	{
		float n[3], mag;

		n[0]=2.0f*((float)Image->Data[Channels*i+0]*OneOver255)-1.0f;
		n[1]=2.0f*((float)Image->Data[Channels*i+1]*OneOver255)-1.0f;
		n[2]=2.0f*((float)Image->Data[Channels*i+2]*OneOver255)-1.0f;

		mag=1.0f/sqrtf(n[0]*n[0]+n[1]*n[1]+n[2]*n[2]);
		n[0]*=mag;
		n[1]*=mag;
		n[2]*=mag;

		Buffer[4*i+0]=(unsigned short)(65535.0f*(0.5f*n[0]+0.5f));
		Buffer[4*i+1]=(unsigned short)(65535.0f*(0.5f*n[1]+0.5f));
		Buffer[4*i+2]=(unsigned short)(65535.0f*(0.5f*n[2]+0.5f));

		if(Channels==4)
			Buffer[4*i+3]=(unsigned short)(Image->Data[4*i+3]<<8);
		else
			Buffer[4*i+3]=65535;
	}

	Image->Depth=64;

	FREE(Image->Data);
	Image->Data=(unsigned char *)Buffer;
}

void _RGBE2Float(Image_t *Image)
{
	int i;
	float *Buffer=NULL;

	if(Image->Depth!=32)
		return;

	Buffer=(float *)malloc(sizeof(float)*Image->Width*Image->Height*4);

	if(Buffer==NULL)
		return;

	for(i=0;i<Image->Width*Image->Height;i++)
	{
		unsigned char *rgbe=&Image->Data[4*i];
		float *rgb=(float *)&Buffer[4*i];

		if(rgbe[3])
		{
			float f=1.0f;
			int i, e=rgbe[3]-(128+8);

			if(e>0)
			{
				for(i=0;i<e;i++)
					f*=2.0f;
			}
			else
			{
				for(i=0;i<-e;i++)
					f/=2.0f;
			}

			rgb[0]=((float)rgbe[2]+0.5f)*f;
			rgb[1]=((float)rgbe[1]+0.5f)*f;
			rgb[2]=((float)rgbe[0]+0.5f)*f;
			rgb[3]=1.0f;
		}
		else
			rgb[0]=rgb[1]=rgb[2]=rgb[3]=0.0f;
	}

	Image->Depth=128;

	FREE(Image->Data);
	Image->Data=(unsigned char *)Buffer;
}

void _Resample(Image_t *Src, Image_t *Dst)
{
	float fx, fy, hx, hy, lx, ly, sx, sy;
	float xPercent, yPercent, Percent;
	float Total[4], Sum;
	int x, y, iy, ix, Index;

	if(Dst->Data==NULL)
		return;

	sx=(float)Src->Width/Dst->Width;
	sy=(float)Src->Height/Dst->Height;

	for(y=0;y<Dst->Height;y++)
	{
		if(Src->Height>Dst->Height)
		{
			fy=((float)y+0.5f)*sy;
			hy=fy+(sy*0.5f);
			ly=fy-(sy*0.5f);
		}
		else
		{
			fy=(float)y*sy;
			hy=fy+0.5f;
			ly=fy-0.5f;
		}

		for(x=0;x<Dst->Width;x++)
		{
			if(Src->Width>Dst->Width)
			{
				fx=((float)x+0.5f)*sx;
				hx=fx+(sx*0.5f);
				lx=fx-(sx*0.5f);
			}
			else
			{
				fx=(float)x*sx;
				hx=fx+0.5f;
				lx=fx-0.5f;
			}

			Total[0]=Total[1]=Total[2]=Total[3]=Sum=0.0f;

			fy=ly;
			iy=(int)fy;

			while(fy<hy)
			{
				if(hy<iy+1)
					yPercent=hy-fy;
				else
					yPercent=(iy+1)-fy;

				fx=lx;
				ix=(int)fx;

				while(fx<hx)
				{
					if(hx<ix+1)
						xPercent=hx-fx;
					else
						xPercent=(ix+1)-fx;

					Percent=xPercent*xPercent;
					Sum+=Percent;

					Index=min(Src->Height-1, iy)*Src->Width+min(Src->Width-1, ix);

					switch(Src->Depth)
					{
						case 128:
							Total[0]+=((float *)Src->Data)[4*Index+0]*Percent;
							Total[1]+=((float *)Src->Data)[4*Index+1]*Percent;
							Total[2]+=((float *)Src->Data)[4*Index+2]*Percent;
							Total[3]+=((float *)Src->Data)[4*Index+3]*Percent;
							break;

						case 96:
							Total[0]+=((float *)Src->Data)[3*Index+0]*Percent;
							Total[1]+=((float *)Src->Data)[3*Index+1]*Percent;
							Total[2]+=((float *)Src->Data)[3*Index+2]*Percent;
							break;

						case 64:
							Total[0]+=((unsigned short *)Src->Data)[4*Index+0]*Percent;
							Total[1]+=((unsigned short *)Src->Data)[4*Index+1]*Percent;
							Total[2]+=((unsigned short *)Src->Data)[4*Index+2]*Percent;
							Total[3]+=((unsigned short *)Src->Data)[4*Index+3]*Percent;
							break;

						case 48:
							Total[0]+=((unsigned short *)Src->Data)[3*Index+0]*Percent;
							Total[1]+=((unsigned short *)Src->Data)[3*Index+1]*Percent;
							Total[2]+=((unsigned short *)Src->Data)[3*Index+2]*Percent;
							break;

						case 32:
							Total[0]+=(float)Src->Data[4*Index+0]*Percent;
							Total[1]+=(float)Src->Data[4*Index+1]*Percent;
							Total[2]+=(float)Src->Data[4*Index+2]*Percent;
							Total[3]+=(float)Src->Data[4*Index+3]*Percent;
							break;

						case 24:
							Total[0]+=Src->Data[3*Index+0]*Percent;
							Total[1]+=Src->Data[3*Index+1]*Percent;
							Total[2]+=Src->Data[3*Index+2]*Percent;
							break;

						case 16:
							Total[0]+=((((unsigned short *)Src->Data)[Index]>>0x0)&0x1F)*Percent;
							Total[1]+=((((unsigned short *)Src->Data)[Index]>>0x5)&0x1F)*Percent;
							Total[2]+=((((unsigned short *)Src->Data)[Index]>>0xA)&0x1F)*Percent;
							break;

						case 8:
							Total[0]+=Src->Data[Index]*Percent;
					}

					ix++;
					fx=(float)ix;
				}

				iy++;
				fy=(float)iy;
			}

			Index=y*Dst->Width+x;
			Sum=1.0f/Sum;

			switch(Dst->Depth)
			{
				case 128:
					((float *)Dst->Data)[4*Index+0]=(float)(Total[0]*Sum);
					((float *)Dst->Data)[4*Index+1]=(float)(Total[1]*Sum);
					((float *)Dst->Data)[4*Index+2]=(float)(Total[2]*Sum);
					((float *)Dst->Data)[4*Index+3]=(float)(Total[3]*Sum);
					break;

				case 96:
					((float *)Dst->Data)[3*Index+0]=(float)(Total[0]*Sum);
					((float *)Dst->Data)[3*Index+1]=(float)(Total[1]*Sum);
					((float *)Dst->Data)[3*Index+2]=(float)(Total[2]*Sum);
					break;

				case 64:
					((unsigned short *)Dst->Data)[4*Index+0]=(unsigned short)(Total[0]*Sum);
					((unsigned short *)Dst->Data)[4*Index+1]=(unsigned short)(Total[1]*Sum);
					((unsigned short *)Dst->Data)[4*Index+2]=(unsigned short)(Total[2]*Sum);
					((unsigned short *)Dst->Data)[4*Index+3]=(unsigned short)(Total[3]*Sum);
					break;

				case 48:
					((unsigned short *)Dst->Data)[3*Index+0]=(unsigned short)(Total[0]*Sum);
					((unsigned short *)Dst->Data)[3*Index+1]=(unsigned short)(Total[1]*Sum);
					((unsigned short *)Dst->Data)[3*Index+2]=(unsigned short)(Total[2]*Sum);
					break;

				case 32:
					((unsigned char *)Dst->Data)[4*Index+0]=(unsigned char)(Total[0]*Sum);
					((unsigned char *)Dst->Data)[4*Index+1]=(unsigned char)(Total[1]*Sum);
					((unsigned char *)Dst->Data)[4*Index+2]=(unsigned char)(Total[2]*Sum);
					((unsigned char *)Dst->Data)[4*Index+3]=(unsigned char)(Total[3]*Sum);
					break;

				case 24:
					((unsigned char *)Dst->Data)[3*Index+0]=(unsigned char)(Total[0]*Sum);
					((unsigned char *)Dst->Data)[3*Index+1]=(unsigned char)(Total[1]*Sum);
					((unsigned char *)Dst->Data)[3*Index+2]=(unsigned char)(Total[2]*Sum);
					break;

				case 16:
					((unsigned short *)Dst->Data)[Index]=((unsigned short)((Total[0]*Sum))&0x1F)<<0x0|((unsigned short)(Total[1]*Sum)&0x1F)<<0x5|((unsigned short)(Total[2]*Sum)&0x1F)<<0xA;
					break;

				case 8:
					((unsigned char *)Dst->Data)[Index]=(unsigned char)(Total[0]*Sum);
					break;
			}
		}
	}
}

void _BuildMipmaps(Image_t *Image, unsigned int Target)
{
	int i=0, levels;
	unsigned int MaxSize;
	Image_t Dst;

	glGetIntegerv(GL_MAX_TEXTURE_SIZE, &MaxSize);

	Dst.Depth=Image->Depth;
	Dst.Width=min(MaxSize, NextPower2(Image->Width));
	Dst.Height=min(MaxSize, NextPower2(Image->Height));

	if(Dst.Height>Dst.Width)
		levels=ComputeLog(Dst.Height);
	else
		levels=ComputeLog(Dst.Width);

	while(i<=levels)
	{
		Dst.Data=(unsigned char *)malloc(Dst.Width*Dst.Height*(Dst.Depth>>3));

		_Resample(Image, &Dst);

		switch(Dst.Depth)
		{
			case 128:
				glTexImage2D(Target, i, GL_RGBA, Dst.Width, Dst.Height, 0, GL_RGBA, GL_FLOAT, Dst.Data);
				break;

			case 96:
				glTexImage2D(Target, i, GL_RGB, Dst.Width, Dst.Height, 0, GL_RGB, GL_FLOAT, Dst.Data);
				break;

			case 64:
				glTexImage2D(Target, i, GL_RGBA, Dst.Width, Dst.Height, 0, GL_RGBA, GL_UNSIGNED_SHORT, Dst.Data);
				break;

			case 48:
				glTexImage2D(Target, i, GL_RGB, Dst.Width, Dst.Height, 0, GL_RGB, GL_UNSIGNED_SHORT, Dst.Data);
				break;

			case 32:
				glTexImage2D(Target, i, GL_RGBA, Dst.Width, Dst.Height, 0, GL_RGBA, GL_UNSIGNED_BYTE, Dst.Data);
				break;

			case 24:
				glTexImage2D(Target, i, GL_RGB, Dst.Width, Dst.Height, 0, GL_RGB, GL_UNSIGNED_BYTE, Dst.Data);
				break;

			case 16:
				glTexImage2D(Target, i, GL_RGB, Dst.Width, Dst.Height, 0, GL_RGBA, GL_UNSIGNED_SHORT_5_6_5, Dst.Data);
				break;

			case 8:
				glTexImage2D(Target, i, GL_LUMINANCE, Dst.Width, Dst.Height, 0, GL_LUMINANCE, GL_UNSIGNED_BYTE, Dst.Data);
				break;
		}

		FREE(Dst.Data);

		Dst.Width=(Dst.Width>1)?Dst.Width>>1:Dst.Width;
		Dst.Height=(Dst.Height>1)?Dst.Height>>1:Dst.Height;
		i++;
	}
}

void _GetPixelBilinear(Image_t *Image, float x, float y, unsigned char *Out)
{
	int ix=(int)x, iy=(int)y;
	int ox=ix+1, oy=iy+1;
	float fx=x-ix, fy=y-iy;
	float w00, w01, w10, w11;

	if(ox>=Image->Width)
		ox=Image->Width-1;

	if(oy>=Image->Height)
		oy=Image->Height-1;

	if(fx<0.0f)
		ix=ox=0;

	if(fy<0.0f)
		iy=oy=0;

	w11=fx*fy;
	w00=1.0f-fx-fy+w11;
	w10=fx-w11;
	w01=fy-w11;

	switch(Image->Depth)
	{
		case 128:
			((float *)Out)[0]=((float *)Image->Data)[4*(iy*Image->Width+ix)+0]*w00+((float *)Image->Data)[4*(iy*Image->Width+ox)+0]*w10+((float *)Image->Data)[4*(oy*Image->Width+ix)+0]*w01+((float *)Image->Data)[4*(oy*Image->Width+ox)+0]*w11;
			((float *)Out)[1]=((float *)Image->Data)[4*(iy*Image->Width+ix)+1]*w00+((float *)Image->Data)[4*(iy*Image->Width+ox)+1]*w10+((float *)Image->Data)[4*(oy*Image->Width+ix)+1]*w01+((float *)Image->Data)[4*(oy*Image->Width+ox)+1]*w11;
			((float *)Out)[2]=((float *)Image->Data)[4*(iy*Image->Width+ix)+2]*w00+((float *)Image->Data)[4*(iy*Image->Width+ox)+2]*w10+((float *)Image->Data)[4*(oy*Image->Width+ix)+2]*w01+((float *)Image->Data)[4*(oy*Image->Width+ox)+2]*w11;
			((float *)Out)[3]=((float *)Image->Data)[4*(iy*Image->Width+ix)+3]*w00+((float *)Image->Data)[4*(iy*Image->Width+ox)+3]*w10+((float *)Image->Data)[4*(oy*Image->Width+ix)+3]*w01+((float *)Image->Data)[4*(oy*Image->Width+ox)+3]*w11;
			break;

		case 64:
			((unsigned short *)Out)[0]=(unsigned short)(((unsigned short *)Image->Data)[4*(iy*Image->Width+ix)+0]*w00+((unsigned short *)Image->Data)[4*(iy*Image->Width+ox)+0]*w10+((unsigned short *)Image->Data)[4*(oy*Image->Width+ix)+0]*w01+((unsigned short *)Image->Data)[4*(oy*Image->Width+ox)+0]*w11);
			((unsigned short *)Out)[1]=(unsigned short)(((unsigned short *)Image->Data)[4*(iy*Image->Width+ix)+1]*w00+((unsigned short *)Image->Data)[4*(iy*Image->Width+ox)+1]*w10+((unsigned short *)Image->Data)[4*(oy*Image->Width+ix)+1]*w01+((unsigned short *)Image->Data)[4*(oy*Image->Width+ox)+1]*w11);
			((unsigned short *)Out)[2]=(unsigned short)(((unsigned short *)Image->Data)[4*(iy*Image->Width+ix)+2]*w00+((unsigned short *)Image->Data)[4*(iy*Image->Width+ox)+2]*w10+((unsigned short *)Image->Data)[4*(oy*Image->Width+ix)+2]*w01+((unsigned short *)Image->Data)[4*(oy*Image->Width+ox)+2]*w11);
			((unsigned short *)Out)[3]=(unsigned short)(((unsigned short *)Image->Data)[4*(iy*Image->Width+ix)+3]*w00+((unsigned short *)Image->Data)[4*(iy*Image->Width+ox)+3]*w10+((unsigned short *)Image->Data)[4*(oy*Image->Width+ix)+3]*w01+((unsigned short *)Image->Data)[4*(oy*Image->Width+ox)+3]*w11);
			break;

		case 32:
			Out[0]=(unsigned char)(Image->Data[4*(iy*Image->Width+ix)+0]*w00+Image->Data[4*(iy*Image->Width+ox)+0]*w10+Image->Data[4*(oy*Image->Width+ix)+0]*w01+Image->Data[4*(oy*Image->Width+ox)+0]*w11);
			Out[1]=(unsigned char)(Image->Data[4*(iy*Image->Width+ix)+1]*w00+Image->Data[4*(iy*Image->Width+ox)+1]*w10+Image->Data[4*(oy*Image->Width+ix)+1]*w01+Image->Data[4*(oy*Image->Width+ox)+1]*w11);
			Out[2]=(unsigned char)(Image->Data[4*(iy*Image->Width+ix)+2]*w00+Image->Data[4*(iy*Image->Width+ox)+2]*w10+Image->Data[4*(oy*Image->Width+ix)+2]*w01+Image->Data[4*(oy*Image->Width+ox)+2]*w11);
			Out[3]=(unsigned char)(Image->Data[4*(iy*Image->Width+ix)+3]*w00+Image->Data[4*(iy*Image->Width+ox)+3]*w10+Image->Data[4*(oy*Image->Width+ix)+3]*w01+Image->Data[4*(oy*Image->Width+ox)+3]*w11);
			break;

		case 16:
		{
			unsigned short p0=((unsigned short *)Image->Data)[iy*Image->Width+ix];
			unsigned short p1=((unsigned short *)Image->Data)[iy*Image->Width+ox];
			unsigned short p2=((unsigned short *)Image->Data)[oy*Image->Width+ix];
			unsigned short p3=((unsigned short *)Image->Data)[oy*Image->Width+ox];

			*((unsigned short *)Out) =(unsigned short)(((p0>>0x0)&0x1F)*w00+((p1>>0x0)&0x1F)*w10+((p2>>0x0)&0x1F)*w01+((p3>>0x0)&0x1F)*w11)<<0x0;
			*((unsigned short *)Out)|=(unsigned short)(((p0>>0x5)&0x1F)*w00+((p1>>0x5)&0x1F)*w10+((p2>>0x5)&0x1F)*w01+((p3>>0x5)&0x1F)*w11)<<0x5;
			*((unsigned short *)Out)|=(unsigned short)(((p0>>0xA)&0x1F)*w00+((p1>>0xA)&0x1F)*w10+((p2>>0xA)&0x1F)*w01+((p3>>0xA)&0x1F)*w11)<<0xA;
			break;
		}

		case 8:
			*Out=(unsigned char)(Image->Data[iy*Image->Width+ix]*w00+Image->Data[iy*Image->Width+ox]*w10+Image->Data[oy*Image->Width+ix]*w01+Image->Data[oy*Image->Width+ox]*w11);
			break;
	}
}

void _GetUVAngularMap(float xyz[3], float *uv)
{
	float phi=-acosf(xyz[2]), theta=atan2f(xyz[1], xyz[0]);

	uv[0]=0.5f*((phi/3.1415926f)*cosf(theta))+0.5f;
	uv[1]=0.5f*((phi/3.1415926f)*sinf(theta))+0.5f;
}

void _GetXYZFace(float uv[2], float *xyz, int face)
{
	float mag;

	switch(face)
	{
		// +X
		case 0:
			xyz[0]=1.0f;
			xyz[1]=(uv[1]-0.5f)*2.0f;
			xyz[2]=(0.5f-uv[0])*2.0f;
			break;

		// -X
		case 1:
			xyz[0]=-1.0f;
			xyz[1]=(uv[1]-0.5f)*2.0f;
			xyz[2]=(uv[0]-0.5f)*2.0f;
			break;

		// +Y
		case 2:
			xyz[0]=(uv[0]-0.5f)*2.0f;
			xyz[1]=-1.0f;
			xyz[2]=(uv[1]-0.5f)*2.0f;
			break;

		// -Y
		case 3:
			xyz[0]=(uv[0]-0.5f)*2.0f;
			xyz[1]=1.0f;
			xyz[2]=(0.5f-uv[1])*2.0f;
			break;

		// +Z
		case 4:
			xyz[0]=(uv[0]-0.5f)*2.0f;
			xyz[1]=(uv[1]-0.5f)*2.0f;
			xyz[2]=1.0f;
			break;

		// -Z
		case 5:
			xyz[0]=(0.5f-uv[0])*2.0f;
			xyz[1]=(uv[1]-0.5f)*2.0f;
			xyz[2]=-1.0f;
			break;
	}

	mag=sqrtf(xyz[0]*xyz[0]+xyz[1]*xyz[1]+xyz[2]*xyz[2]);

	if(mag)
	{
		mag=1.0f/mag;
		xyz[0]*=mag;
		xyz[1]*=mag;
		xyz[2]*=mag;
	}
}

void _AngularMapFace(Image_t *In, int Face, int Mipmap)
{
	Image_t Out;
	int x, y;
	unsigned int Internal, External, Type;

	switch(In->Depth)
	{
		case 128:
			Internal=GL_RGBA;
			External=GL_RGBA;
			Type=GL_FLOAT;
			break;

		case 96:
			Internal=GL_RGB;
			External=GL_RGB;
			Type=GL_FLOAT;
			break;

		case 64:
			Internal=GL_RGBA;
			External=GL_RGBA;
			Type=GL_UNSIGNED_SHORT;
			break;

		case 32:
			Internal=GL_RGBA;
			External=GL_RGBA;
			Type=GL_UNSIGNED_BYTE;
			break;

		case 24:
			Internal=GL_RGB;
			External=GL_RGB;
			Type=GL_UNSIGNED_BYTE;
			break;

 		case 16:
			Internal=GL_RGB;
			External=GL_RGB;
			Type=GL_UNSIGNED_SHORT_5_6_5;
			break;

		case 8:
			Internal=GL_LUMINANCE;
			External=GL_LUMINANCE;
			Type=GL_UNSIGNED_BYTE;
			break;

		default:
			return;
	}

	Out.Depth=In->Depth;
	Out.Width=NextPower2(In->Width>>1);
	Out.Height=NextPower2(In->Height>>1);
	Out.Data=(unsigned char *)malloc(Out.Width*Out.Height*(Out.Depth>>3));

	for(y=0;y<Out.Height;y++)
	{
		float fy=(float)y/(Out.Height-1);

		for(x=0;x<Out.Width;x++)
		{
			float fx=(float)x/(Out.Width-1);
			float uv[2]={ fx, fy }, xyz[3];

			_GetXYZFace(uv, xyz, Face);
			_GetUVAngularMap(xyz, uv);

			_GetPixelBilinear(In, uv[0]*In->Width, uv[1]*In->Height, &Out.Data[(Out.Depth>>3)*(y*Out.Width+x)]);
		}
	}

	if(Mipmap)
		_BuildMipmaps(&Out, GL_TEXTURE_CUBE_MAP_POSITIVE_X+Face);
	else
		glTexImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_X+Face, 0, Internal, Out.Width, Out.Height, 0, External, Type, Out.Data);

	FREE(Out.Data);
}

unsigned int Image_Upload(char *Filename, unsigned long Flags)
{
	unsigned int TextureID=0;
	unsigned int Target=GL_TEXTURE_2D;
	Image_t Image;
	char *Extension=strrchr(Filename, '.');

	if(Extension!=NULL)
	{
		if(!strcmp(Extension, ".tga"))
		{
			if(!TGA_Load(Filename, &Image))
			{
				LOGI("%s failed load.\n", Filename);
				return 0;
			}
			else
				_BGRtoRGB(&Image);
		}
		else
			return 0;
	}

	glPixelStorei(GL_UNPACK_ALIGNMENT, 1);

	if(Flags&IMAGE_CUBEMAP_ANGULAR)
		Target=GL_TEXTURE_CUBE_MAP;

	if(Flags&IMAGE_RECTANGLE)
	{
		return 0;

		if(Flags&IMAGE_AUTOMIPMAP)
			Flags^=IMAGE_AUTOMIPMAP;

		if(Flags&IMAGE_MIPMAP)
			Flags^=IMAGE_MIPMAP;

		if(Flags&IMAGE_REPEAT)
		{
			Flags^=IMAGE_REPEAT;
			Flags|=IMAGE_CLAMP;
		}
	}

	glGenTextures(1, &TextureID);
	glBindTexture(Target, TextureID);

	glTexParameteri(Target, GL_TEXTURE_WRAP_S, GL_REPEAT);
	glTexParameteri(Target, GL_TEXTURE_WRAP_T, GL_REPEAT);
	glTexParameteri(Target, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glTexParameteri(Target, GL_TEXTURE_MIN_FILTER, GL_LINEAR);

	if(Flags&IMAGE_NEAREST)
	{
		glTexParameteri(Target, GL_TEXTURE_MAG_FILTER, GL_NEAREST);

		if(Flags&IMAGE_MIPMAP)
			glTexParameteri(Target, GL_TEXTURE_MIN_FILTER, GL_NEAREST_MIPMAP_NEAREST);
		else
			glTexParameteri(Target, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
	}

	if(Flags&IMAGE_BILINEAR)
	{
		glTexParameteri(Target, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

		if(Flags&IMAGE_MIPMAP)
			glTexParameteri(Target, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_NEAREST);
		else
			glTexParameteri(Target, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	}

	if(Flags&IMAGE_TRILINEAR)
	{
		glTexParameteri(Target, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

		if(Flags&IMAGE_MIPMAP)
			glTexParameteri(Target, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
		else
			glTexParameteri(Target, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	}

	if(Flags&IMAGE_CLAMP_EDGE_U)
		glTexParameteri(Target, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);

	if(Flags&IMAGE_CLAMP_EDGE_V)
		glTexParameteri(Target, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

	if(Flags&IMAGE_REPEAT_U)
		glTexParameteri(Target, GL_TEXTURE_WRAP_S, GL_REPEAT);

	if(Flags&IMAGE_REPEAT_V)
		glTexParameteri(Target, GL_TEXTURE_WRAP_T, GL_REPEAT);

	if(Flags&IMAGE_RGBE)
		_RGBE2Float(&Image);

	if(Flags&IMAGE_NORMALMAP)
		_MakeNormalMap(&Image);

	if(Flags&IMAGE_NORMALIZE)
		_Normalize(&Image);

	if(Flags&IMAGE_CUBEMAP_ANGULAR)
	{
		_AngularMapFace(&Image, 0, Flags&IMAGE_MIPMAP);
		_AngularMapFace(&Image, 1, Flags&IMAGE_MIPMAP);
		_AngularMapFace(&Image, 2, Flags&IMAGE_MIPMAP);
		_AngularMapFace(&Image, 3, Flags&IMAGE_MIPMAP);
		_AngularMapFace(&Image, 4, Flags&IMAGE_MIPMAP);
		_AngularMapFace(&Image, 5, Flags&IMAGE_MIPMAP);

		FREE(Image.Data);

		return TextureID;
	}

	switch(Image.Depth)
	{
		case IMAGE_DXT5:
			break;

		case IMAGE_DXT3:
			break;

		case IMAGE_DXT1:
			break;

		case 128:
			if(Flags&IMAGE_AUTOMIPMAP)
				glTexImage2D(Target, 0, GL_RGBA, Image.Width, Image.Height, 0, GL_RGBA, GL_FLOAT, Image.Data);
			else
			{
				if(Flags&IMAGE_MIPMAP)
					_BuildMipmaps(&Image, Target);
				else
					glTexImage2D(Target, 0, GL_RGBA, Image.Width, Image.Height, 0, GL_RGBA, GL_FLOAT, Image.Data);
			}
			break;

		case 96:
			if(Flags&IMAGE_AUTOMIPMAP)
				glTexImage2D(Target, 0, GL_RGB, Image.Width, Image.Height, 0, GL_RGB, GL_FLOAT, Image.Data);
			else
			{
				if(Flags&IMAGE_MIPMAP)
					_BuildMipmaps(&Image, Target);
				else
					glTexImage2D(Target, 0, GL_RGB, Image.Width, Image.Height, 0, GL_RGB, GL_FLOAT, Image.Data);
			}
			break;

		case 64:
			if(Flags&IMAGE_AUTOMIPMAP)
				glTexImage2D(Target, 0, GL_RGBA, Image.Width, Image.Height, 0, GL_RGBA, GL_UNSIGNED_SHORT, Image.Data);
			else
			{
				if(Flags&IMAGE_MIPMAP)
					_BuildMipmaps(&Image, Target);
				else
					glTexImage2D(Target, 0, GL_RGBA, Image.Width, Image.Height, 0, GL_RGBA, GL_UNSIGNED_SHORT, Image.Data);
			}
			break;

		case 32:
			if(Flags&IMAGE_AUTOMIPMAP)
				glTexImage2D(Target, 0, GL_RGBA, Image.Width, Image.Height, 0, GL_RGBA, GL_UNSIGNED_BYTE, Image.Data);
			else
			{
				if(Flags&IMAGE_MIPMAP)
					_BuildMipmaps(&Image, Target);
				else
					glTexImage2D(Target, 0, GL_RGBA, Image.Width, Image.Height, 0, GL_RGBA, GL_UNSIGNED_BYTE, Image.Data);
			}
			break;

		case 24:
			if(Flags&IMAGE_AUTOMIPMAP)
				glTexImage2D(Target, 0, GL_RGB, Image.Width, Image.Height, 0, GL_RGB, GL_UNSIGNED_BYTE, Image.Data);
			else
			{
				if(Flags&IMAGE_MIPMAP)
					_BuildMipmaps(&Image, Target);
				else
					glTexImage2D(Target, 0, GL_RGB, Image.Width, Image.Height, 0, GL_RGB, GL_UNSIGNED_BYTE, Image.Data);
			}
			break;

		case 16:
			if(Flags&IMAGE_AUTOMIPMAP)
				glTexImage2D(Target, 0, GL_RGB, Image.Width, Image.Height, 0, GL_RGB, GL_UNSIGNED_SHORT_5_6_5, Image.Data);
			else
			{
				if(Flags&IMAGE_MIPMAP)
					_BuildMipmaps(&Image, Target);
				else
					glTexImage2D(Target, 0, GL_RGB, Image.Width, Image.Height, 0, GL_RGB, GL_UNSIGNED_SHORT_5_6_5, Image.Data);
			}
			break;

		case 8:
			if(Flags&IMAGE_AUTOMIPMAP)
				glTexImage2D(Target, 0, GL_LUMINANCE, Image.Width, Image.Height, 0, GL_LUMINANCE, GL_UNSIGNED_BYTE, Image.Data);
			else
			{
				if(Flags&IMAGE_MIPMAP)
					_BuildMipmaps(&Image, Target);
				else
					glTexImage2D(Target, 0, GL_LUMINANCE, Image.Width, Image.Height, 0, GL_LUMINANCE, GL_UNSIGNED_BYTE, Image.Data);
			}
			break;

		default:
			FREE(Image.Data);
			return 0;
	}

	FREE(Image.Data);

	return TextureID;
}
