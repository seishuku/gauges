/*
Copyright 2012 Matt Williams
Main "engine" code, process entry, setup, etc
*/

#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <termios.h>
#include <EGL/egl.h>
#include <GLES2/gl2.h>
#include <android/log.h>
#include <android/configuration.h>
#include "android_native_app_glue.h"
#include "_math.h"
#include "image.h"
#include "cltTable.h"
#include "matTable.h"
#include "tpsTable.h"

#define LOGI(...) ((void)__android_log_print(ANDROID_LOG_INFO, "native-activity", __VA_ARGS__))
#define LOGW(...) ((void)__android_log_print(ANDROID_LOG_WARN, "native-activity", __VA_ARGS__))

struct android_app *app;

AAssetManager *assetManager;

EGLDisplay display;
EGLSurface surface;
EGLContext context;

int32_t width, height;

typedef enum
{
	TEXTURE_FONT,
	TEXTURE_AFR,
	TEXTURE_BAT,
	TEXTURE_CLT,
	TEXTURE_IGN,
	TEXTURE_MAP,
	TEXTURE_MAT,
	TEXTURE_RPM,
	TEXTURE_TPS,

	GLSL_FONT_SHADER,
	GLSL_FONT_VVERT,
	GLSL_FONT_TEX,
	GLSL_FONT_WIDTH,
	GLSL_FONT_HEIGHT,

	GLSL_GAUGE_SHADER,
	GLSL_GAUGE_VVERT,
	GLSL_GAUGE_TEX,
	GLSL_GAUGE_AMOUNT,
	GLSL_GAUGE_WIDTH,
	GLSL_GAUGE_HEIGHT,

	NUM_OBJECTS
} ObjectNames;

unsigned int Objects[NUM_OBJECTS];

float PanX=0.0f, PanY=0.0f, Zoom=0.0f;
float RotateX=0.0f, RotateY=0.0f;

struct timespec StartTime, EndTime;
float FPS, FPSTemp=0.0f, fTimeStep, fTime=0.0f;
int Frames=0;

typedef struct
{
	unsigned char secl;
	unsigned char squirt;
	unsigned char engine;
	unsigned char baroADC;
	unsigned char mapADC;
	unsigned char matADC;
	unsigned char cltADC;
	unsigned char tpsADC;
	unsigned char batADC;
	unsigned char egoADC;
	unsigned char egoCorrection;
	unsigned char airCorrection;
	unsigned char warmupEnrich;
	unsigned char rpm100;
	unsigned char pulsewidth1;
	unsigned char accelEnrich;
	unsigned char baroCorrection;
	unsigned char gammaEnrich;
	unsigned char veCurr1;
	unsigned char pulsewidth2;
	unsigned char veCurr2;
	unsigned char idleDC;
	unsigned short iTime;
	unsigned char advance;
	unsigned char afrTarget;
	unsigned char fuelADC;
	unsigned char egtADC;
	unsigned char CltIatAngle;
	unsigned char KnockAngle;
	unsigned char egoCorrection2;
	unsigned char porta;
	unsigned char portb;
	unsigned char portc;
	unsigned char portd;
	unsigned char stackL;
	unsigned char tpsLast;
	unsigned char iTimeX;
	unsigned char bcdc;
} MegaSquirtData_t;

float RPM, CLT, TPS, BAT, MAP, MAT, AFR, ADV;

MegaSquirtData_t MSDat;
char MSVer[100];
int tty_fd;

int SerialConnect(char *port)
{
	struct termios tio;
	char cmd='T';

	memset(MSVer, 0, 100);
	sprintf(MSVer, "No connection.");
	memset(&MSDat, 0, sizeof(MegaSquirtData_t));

	memset(&tio, 0, sizeof(tio));
	tio.c_iflag=0;
	tio.c_oflag=0;
	tio.c_cflag=CS8|CREAD|CLOCAL;					// 8n1, see termios.h for more information
	tio.c_lflag=0;
	tio.c_cc[VMIN]=1;
	tio.c_cc[VTIME]=5;

	tty_fd=open(port, O_RDWR);

	cfsetospeed(&tio, B9600);
	cfsetispeed(&tio, B9600);

	tcsetattr(tty_fd, TCSANOW, &tio);

	if(tty_fd!=-1)
	{
		write(tty_fd, &cmd, 1);
		read(tty_fd, MSVer, 32);
		MSVer[32]='\0';
	}
	else
		return 0;

	return 1;
}

void PullData(void)
{
	char cmd='R';

	if(tty_fd!=-1)
	{
		write(tty_fd, &cmd, 1);
		read(tty_fd, &MSDat, 39);
	}
}

int64_t timespecDiff(struct timespec *timeA_p, struct timespec *timeB_p)
{
	return ((timeA_p->tv_sec*1000000000)+timeA_p->tv_nsec)-((timeB_p->tv_sec*1000000000)+timeB_p->tv_nsec);
}

int LoadShader(GLuint Shader, char *Filename)
{
	AAsset *stream;
	char *buffer;
	int length;

	if((stream=AAssetManager_open(assetManager, Filename, AASSET_MODE_BUFFER))==NULL)
		return 0;

	length=AAsset_getLength(stream);

	buffer=(char *)malloc(length+1);
	AAsset_read(stream, buffer, length);
	buffer[length]='\0';

	glShaderSource(Shader, 1, (const char **)&buffer, &length);

	AAsset_close(stream);
	free(buffer);

	return 1;
}

GLuint CreateProgram(char *VertexFilename, char *FragmentFilename)
{
	GLuint Program, Vertex, Fragment;
	GLint Status, LogLength;
	char *Log=NULL;

	Program=glCreateProgram();

	if(VertexFilename)
	{
		Vertex=glCreateShader(GL_VERTEX_SHADER);

		if(LoadShader(Vertex, VertexFilename))
		{
			glCompileShader(Vertex);
			glGetShaderiv(Vertex, GL_COMPILE_STATUS, &Status);

			if(!Status)
			{
				glGetShaderiv(Vertex, GL_INFO_LOG_LENGTH, &LogLength);
				Log=(char *)malloc(LogLength);

				if(Log)
				{
					glGetShaderInfoLog(Vertex, LogLength, NULL, Log);
					LOGI("%s - %s\n", VertexFilename, Log);
					free(Log);
				}
			}
			else
				glAttachShader(Program, Vertex);
		}

		glDeleteShader(Vertex);
	}

	if(FragmentFilename)
	{
		Fragment=glCreateShader(GL_FRAGMENT_SHADER);

		if(LoadShader(Fragment, FragmentFilename))
		{
			glCompileShader(Fragment);
			glGetShaderiv(Fragment, GL_COMPILE_STATUS, &Status);

			if(!Status)
			{
				glGetShaderiv(Fragment, GL_INFO_LOG_LENGTH, &LogLength);
				Log=(char *)malloc(LogLength);

				if(Log)
				{
					glGetShaderInfoLog(Fragment, LogLength, NULL, Log);
					LOGI("%s - %s\n", FragmentFilename, Log);
					free(Log);
				}
			}
			else
				glAttachShader(Program, Fragment);
		}

		glDeleteShader(Fragment);
	}

	glLinkProgram(Program);
	glGetProgramiv(Program, GL_LINK_STATUS, &Status);

	if(!Status)
	{
		glGetProgramiv(Program, GL_INFO_LOG_LENGTH, &LogLength);
		Log=(char *)malloc(LogLength);

		if(Log)
		{
			glGetProgramInfoLog(Program, LogLength, NULL, Log);
			LOGI("Link - %s\n", Log);
			free(Log);
		}
	}

	return Program;
}

int engine_init_display(void)
{
	EGLint attribs[]=
	{
		EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
		EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
		EGL_NATIVE_RENDERABLE, EGL_FALSE,
		EGL_BUFFER_SIZE, 32,
		EGL_DEPTH_SIZE, 24,
		EGL_SAMPLES, 1,
		EGL_NONE
	};
	EGLint contextAttribs[]=
	{
		EGL_CONTEXT_CLIENT_VERSION, 2,
		EGL_NONE
	};
	EGLint format=0, numConfig=0;
	EGLint major=0, minor=0;
	EGLConfig eglConfig;

	if((display=eglGetDisplay((EGLNativeDisplayType)EGL_DEFAULT_DISPLAY))==EGL_NO_DISPLAY)
	{
		LOGI("eglGetDisplay failed.\n");
		return 0;
	}

	if(!eglInitialize(display, &major, &minor))
	{
		LOGI("eglInitialize failed.\n");
		return 0;
	}

	if(major<1||minor<3)
	{
		LOGI("System does not support at least EGL 1.3\n");
		return 0;
	}

	if(!eglChooseConfig(display, attribs, &eglConfig, 1, &numConfig))
	{
		LOGI("eglChooseConfig failed.\n");
		return 0;
	}

	if((surface=eglCreateWindowSurface(display, eglConfig, (EGLNativeWindowType)app->window, NULL))==EGL_NO_SURFACE)
	{
		LOGI("eglCreateWindowSurface failed.\n");
		return 0;
	}

	if((context=eglCreateContext(display, eglConfig, EGL_NO_CONTEXT, contextAttribs))==EGL_NO_CONTEXT)
	{
		LOGI("eglCreateContext failed.\n");
		return 0;
	}

	if(!eglMakeCurrent(display, surface, surface, context))
	{
		LOGI("eglMakeCurrent failed.\n");
		return 0;
	}

	eglQuerySurface(display, surface, EGL_WIDTH, &width);
	eglQuerySurface(display, surface, EGL_HEIGHT, &height);

	LOGI("Width: %d Height: %d\n", width, height);

	glViewport(0, 0, width, height);

	Objects[GLSL_FONT_SHADER]=CreateProgram("2d_v.glsl", "font_f.glsl");
	glUseProgram(Objects[GLSL_FONT_SHADER]);
	Objects[GLSL_FONT_VVERT]=glGetAttribLocation(Objects[GLSL_FONT_SHADER], "vVert");
	Objects[GLSL_FONT_TEX]=glGetUniformLocation(Objects[GLSL_FONT_SHADER], "Tex");
	Objects[GLSL_FONT_WIDTH]=glGetUniformLocation(Objects[GLSL_FONT_SHADER], "right");
	Objects[GLSL_FONT_HEIGHT]=glGetUniformLocation(Objects[GLSL_FONT_SHADER], "top");

	Objects[TEXTURE_FONT]=Image_Upload("font.tga", IMAGE_BILINEAR|IMAGE_REPEAT);

	Objects[GLSL_GAUGE_SHADER]=CreateProgram("2d_v.glsl", "gauge_f.glsl");
	glUseProgram(Objects[GLSL_GAUGE_SHADER]);
	Objects[GLSL_GAUGE_VVERT]=glGetAttribLocation(Objects[GLSL_GAUGE_SHADER], "vVert");
	Objects[GLSL_GAUGE_TEX]=glGetUniformLocation(Objects[GLSL_GAUGE_SHADER], "Tex");
	Objects[GLSL_GAUGE_AMOUNT]=glGetUniformLocation(Objects[GLSL_GAUGE_SHADER], "Amount");
	Objects[GLSL_GAUGE_WIDTH]=glGetUniformLocation(Objects[GLSL_GAUGE_SHADER], "right");
	Objects[GLSL_GAUGE_HEIGHT]=glGetUniformLocation(Objects[GLSL_GAUGE_SHADER], "top");

	Objects[TEXTURE_AFR]=Image_Upload("afr.tga", IMAGE_BILINEAR|IMAGE_REPEAT);
	Objects[TEXTURE_BAT]=Image_Upload("bat.tga", IMAGE_BILINEAR|IMAGE_REPEAT);
	Objects[TEXTURE_CLT]=Image_Upload("clt.tga", IMAGE_BILINEAR|IMAGE_REPEAT);
	Objects[TEXTURE_IGN]=Image_Upload("ign.tga", IMAGE_BILINEAR|IMAGE_REPEAT);
	Objects[TEXTURE_MAP]=Image_Upload("map.tga", IMAGE_BILINEAR|IMAGE_REPEAT);
	Objects[TEXTURE_MAT]=Image_Upload("mat.tga", IMAGE_BILINEAR|IMAGE_REPEAT);
	Objects[TEXTURE_RPM]=Image_Upload("rpm.tga", IMAGE_BILINEAR|IMAGE_REPEAT);
	Objects[TEXTURE_TPS]=Image_Upload("tps.tga", IMAGE_BILINEAR|IMAGE_REPEAT);

	glEnable(GL_CULL_FACE);
	glDisable(GL_DEPTH_TEST);

	return 1;
}

void drawquad(float x, float y, float size)
{
	float verts[]=
	{
		x, size+y, -1.0f, 1.0f,
		x, y, -1.0f, -1.0f,
		size+x, size+y, 1.0f, 1.0f,
		size+x, y, 1.0f, -1.0f
	};

	glVertexAttribPointer(Objects[GLSL_GAUGE_VVERT], 4, GL_FLOAT, GL_FALSE, 0, verts);
	glEnableVertexAttribArray(Objects[GLSL_GAUGE_VVERT]);

	glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);

	glDisableVertexAttribArray(Objects[GLSL_GAUGE_VVERT]);
}

void Font_Print(float x, float y, char *string, ...)
{
	float *verts=NULL;
	char *ptr, text[4096];
	va_list	ap;
	int sx=x, numchar, i;
	float cx, cy;

	if(string==NULL)
		return;

	va_start(ap, string);
	vsprintf(text, string, ap);
	va_end(ap);

	numchar=strlen(text);

	verts=(float *)malloc(sizeof(float)*4*6*numchar);

	if(verts==NULL)
		return;

	for(ptr=text;*ptr!='\0';ptr++)
	{
		if(*ptr=='\n'||*ptr=='\r')
			y+=12;
	}

	glUseProgram(Objects[GLSL_FONT_SHADER]);
	glUniform1i(Objects[GLSL_FONT_TEX], 0);
	glUniform1f(Objects[GLSL_FONT_WIDTH], (float)width);
	glUniform1f(Objects[GLSL_FONT_HEIGHT], (float)height);

	glActiveTexture(GL_TEXTURE0);
	glBindTexture(GL_TEXTURE_2D, Objects[TEXTURE_FONT]);

	for(ptr=text, i=0;*ptr!='\0';ptr++)
	{
		if(*ptr=='\n'||*ptr=='\r')
		{
			x=sx;
			y-=12;
			continue;
		}

		cx=     (float)(*ptr%16)*0.0625f;
		cy=1.0f-(float)(*ptr/16)*0.0625f;

		verts[i++]=x;
		verts[i++]=y+16.0f;
		verts[i++]=cx;
		verts[i++]=cy;

		verts[i++]=x;
		verts[i++]=y;
		verts[i++]=cx;
		verts[i++]=cy-0.0625f;

		verts[i++]=x+16.0f;
		verts[i++]=y+16.0f;
		verts[i++]=cx+0.0625f;
		verts[i++]=cy;

		verts[i++]=x+16.0f;
		verts[i++]=y+16.0f;
		verts[i++]=cx+0.0625f;
		verts[i++]=cy;

		verts[i++]=x;
		verts[i++]=y;
		verts[i++]=cx;
		verts[i++]=cy-0.0625f;

		verts[i++]=x+16.0f;
		verts[i++]=y;
		verts[i++]=cx+0.0625f;
		verts[i++]=cy-0.0625f;

		x+=8;
	}

	glVertexAttribPointer(Objects[GLSL_FONT_VVERT], 4, GL_FLOAT, GL_FALSE, 0, verts);
	glEnableVertexAttribArray(Objects[GLSL_FONT_VVERT]);

	glDrawArrays(GL_TRIANGLES, 0, 6*numchar);

	glDisableVertexAttribArray(Objects[GLSL_FONT_VVERT]);

	free(verts);
}

unsigned short ShortSwap(unsigned short l)
{
	unsigned char b1=l&255, b2=(l>>8)&255;

	return (b1<<8)+b2;
}

void engine_draw_frame(void)
{
	float h2=(float)height/2.25f;

	if(display==NULL)
        return;

	if(fTime>(1.0f/20.0f))
	{
		float iTimeFull;
		PullData();

		iTimeFull=(MSDat.iTimeX*65536)+ShortSwap(MSDat.iTime);
		RPM=iTimeFull>0?(60000000*2.0f)/(iTimeFull*4):0; //0 to 8000
		CLT=cltTable[MSDat.cltADC]; //-115 to 430
		TPS=tpsTable[MSDat.tpsADC]; //-11 to 128
		BAT=(float)MSDat.batADC/255.0f*30.0f; //7 to 21
		MAP=(float)MSDat.mapADC; //0 to 255
		MAT=matTable[MSDat.matADC]; //-85 to 430
		AFR=10+(MSDat.egoADC*0.039216); //10 to 19.4
		ADV=(MSDat.advance*0.352)-10; //50 to -10

		fTime=0.0f;
	}

	glClear(GL_COLOR_BUFFER_BIT);

	glUseProgram(Objects[GLSL_GAUGE_SHADER]);
	glUniform1i(Objects[GLSL_GAUGE_TEX], 0);
	glUniform1f(Objects[GLSL_GAUGE_WIDTH], (float)width);
	glUniform1f(Objects[GLSL_GAUGE_HEIGHT], (float)height);

	glActiveTexture(GL_TEXTURE0);

	glUniform1f(Objects[GLSL_GAUGE_AMOUNT], RPM/8000.0f);
	glBindTexture(GL_TEXTURE_2D, Objects[TEXTURE_RPM]);
	drawquad(0.0f, h2, h2);

	glUniform1f(Objects[GLSL_GAUGE_AMOUNT], (CLT+40)/(250+40));
	glBindTexture(GL_TEXTURE_2D, Objects[TEXTURE_CLT]);
	drawquad(h2, h2, h2);

	glUniform1f(Objects[GLSL_GAUGE_AMOUNT], TPS/100);
	glBindTexture(GL_TEXTURE_2D, Objects[TEXTURE_TPS]);
	drawquad(h2+h2, h2, h2);

	glUniform1f(Objects[GLSL_GAUGE_AMOUNT], (BAT-7)/(21-7));
	glBindTexture(GL_TEXTURE_2D, Objects[TEXTURE_BAT]);
	drawquad(h2+h2+h2, h2, h2);

	glUniform1f(Objects[GLSL_GAUGE_AMOUNT], MAP/255.0f);
	glBindTexture(GL_TEXTURE_2D, Objects[TEXTURE_MAP]);
	drawquad(0.0f, 0.0f, h2);

	glUniform1f(Objects[GLSL_GAUGE_AMOUNT], (MAT+40)/(220+40));
	glBindTexture(GL_TEXTURE_2D, Objects[TEXTURE_MAT]);
	drawquad(h2, 0.0f, h2);

	glUniform1f(Objects[GLSL_GAUGE_AMOUNT], (AFR-10)/(19-10));
	glBindTexture(GL_TEXTURE_2D, Objects[TEXTURE_AFR]);
	drawquad(h2+h2, 0.0f, h2);

	glUniform1f(Objects[GLSL_GAUGE_AMOUNT], (ADV+10)/(50+10));
	glBindTexture(GL_TEXTURE_2D, Objects[TEXTURE_IGN]);
	drawquad(h2+h2+h2, 0.0f, h2);

	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	glEnable(GL_BLEND);
	Font_Print(0+100, h2+75, "%0.2f", RPM);
	Font_Print(h2+100, h2+75, "%0.2f", CLT);
	Font_Print(h2+h2+100, h2+75, "%0.2f", TPS);
	Font_Print(h2+h2+h2+100, h2+75, "%0.2f", BAT);
	Font_Print(0+100, 0+75, "%0.2f", MAP);
	Font_Print(h2+100, 0+75, "%0.2f", MAT);
	Font_Print(h2+h2+100, 0+75, "%0.2f", AFR);
	Font_Print(h2+h2+h2+100, 0+75, "%0.2f", ADV);
	Font_Print(0, 0, "FPS %0.3f\n%s", FPS, MSVer);
	glDisable(GL_BLEND);

	eglSwapBuffers(display, surface);
}

void engine_term_display(void)
{
	if(display!=EGL_NO_DISPLAY)
	{
		eglMakeCurrent(display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);

		if(context!=EGL_NO_CONTEXT)
			eglDestroyContext(display, context);

		if(surface!=EGL_NO_SURFACE)
			eglDestroySurface(display, surface);

		eglTerminate(display);

	}

	display=EGL_NO_DISPLAY;
    context=EGL_NO_CONTEXT;
    surface=EGL_NO_SURFACE;
}

int32_t engine_handle_input(struct android_app *app, AInputEvent *event)
{
	if(AInputEvent_getType(event)==AINPUT_EVENT_TYPE_MOTION)
	{
		switch(AMotionEvent_getPointerCount(event))
		{
			//Single touch, rotate.
			case 1:
				RotateX+=(AMotionEvent_getX(event, 0)-AMotionEvent_getHistoricalX(event, 0, 0))*1.5f;
				RotateY+=(AMotionEvent_getY(event, 0)-AMotionEvent_getHistoricalY(event, 0, 0))*1.5f;
				break;

			//Pinch zoom
			case 2:
			{
				//Distance between the two touch events
				float t1[2]={ AMotionEvent_getX(event, 0), AMotionEvent_getY(event, 0) };
				float t2[2]={ AMotionEvent_getX(event, 1), AMotionEvent_getY(event, 1) };
				float d[2]={ t1[0]-t2[0], t1[1]-t2[1] };
				float sd=sqrtf(d[0]*d[0]+d[1]*d[1]);
				//Distance between the last two touch events
				float ht1[2]={ AMotionEvent_getHistoricalX(event, 0, 0), AMotionEvent_getHistoricalY(event, 0, 0) };
				float ht2[2]={ AMotionEvent_getHistoricalX(event, 1, 0), AMotionEvent_getHistoricalY(event, 1, 0) };
				float hd[2]={ ht1[0]-ht2[0], ht1[1]-ht2[1] };
				float shd=sqrtf(hd[0]*hd[0]+hd[1]*hd[1]);

				//Delta of the touch events for zoom
				Zoom+=(sd-shd)*0.25f;
				break;
			}
		}

		return 1;
	}

	return 0;
}

void engine_handle_cmd(struct android_app *app, int32_t cmd)
{
	switch(cmd)
	{
		case APP_CMD_SAVE_STATE:
			break;

		case APP_CMD_INIT_WINDOW:
			if(app->window!=NULL)
			{
				engine_init_display();
				engine_draw_frame();
			}
			break;

		case APP_CMD_TERM_WINDOW:
			engine_term_display();
			break;

		case APP_CMD_GAINED_FOCUS:
			break;

		case APP_CMD_LOST_FOCUS:
			break;
	}
}

void android_main(struct android_app *state)
{
	system("su -c \"chmod 0777 /dev/ttyUSB0\"");

	app_dummy();

	state->userData=NULL;
	state->onAppCmd=engine_handle_cmd;
	state->onInputEvent=engine_handle_input;

	app=state;

	assetManager=state->activity->assetManager;

	SerialConnect("/dev/ttyUSB0");

	while(1)
	{
		int ident, events;
		struct android_poll_source *source;

		while((ident=ALooper_pollAll(0, NULL, &events, (void**)&source))>=0)
		{
			if(source!=NULL)
				source->process(state, source);

			if(state->destroyRequested!=0)
			{
				engine_term_display();
				return;
			}
		}

		memcpy(&StartTime, &EndTime, sizeof(struct timespec));
		clock_gettime(CLOCK_MONOTONIC, &EndTime);

		engine_draw_frame();

		fTimeStep=(float)timespecDiff(&EndTime, &StartTime)/1000000000.0f;

		fTime+=fTimeStep;
		FPSTemp+=1.0f/fTimeStep;

		if(Frames++>15)
		{
			FPS=FPSTemp/Frames;
			FPSTemp=0.0f;
			Frames=0;
		}
	}

	close(tty_fd);
}
