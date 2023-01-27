//
// Created by pirate on 2022/7/5.
//
#include "jni.h"
#include "utils/LogUtil.h"

#define NATIVE_TEST_CLASS_NAME "com/hikvision/jni/MyTest"

#ifdef __cplusplus
extern "C" {
#endif

/*
* Copyright (C) 2009 The Android Open Source Project
*
* Licensed under the Apache License, Version 2.0 (the "License");
* you may not use this file except in compliance with the License.
* You may obtain a copy of the License at
*
*      http://www.apache.org/licenses/LICENSE-2.0
*
* Unless required by applicable law or agreed to in writing, software
* distributed under the License is distributed on an "AS IS" BASIS,
* WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
* See the License for the specific language governing permissions and
* limitations under the License.
*/

// OpenGL ES 2.0 code

#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES2/gl2.h>
#include <GLES2/gl2ext.h>

#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <string.h>

#include <assert.h>
#include <errno.h>
#include <sys/mman.h>


#include <fcntl.h>
#include <unistd.h>
#include <sys/time.h>

#include <drm/drm_fourcc.h>
#include <xf86drm.h>


#define ALIGN(_v, _d) (((_v) + ((_d) - 1)) & ~((_d) - 1))

#define ECHK(x) x=x;
#define GCHK(x) x;


typedef struct rk_texture_s{
	int w;
	int h;
	int drm_format;
	int is_afbc;
	int texture_id;
	int need_fbo;
	int fbo_id;
	int drm_fd;
	void * drm_viraddr;
} rk_texture_t;


static void printGLString(const char *name, GLenum s) {
	// fprintf(stderr, "printGLString %s, %d\n", name, s);
	const char *v = (const char *) glGetString(s);
	// int error = glGetError();
	// fprintf(stderr, "glGetError() = %d, result of glGetString = %x\n", error,
	//        (unsigned int) v);
	// if ((v < (const char*) 0) || (v > (const char*) 0x10000))
	//    fprintf(stderr, "GL %s = %s\n", name, v);
	// else
	//    fprintf(stderr, "GL %s = (null) 0x%08x\n", name, (unsigned int) v);
	fprintf(stderr, "GL %s = %s\n", name, v);
}


static void checkGlError(const char* op) {
	for (GLint error = glGetError(); error; error
													= glGetError()) {
		fprintf(stderr, "after %s() glError (0x%x)\n", op, error);
	}
}

static const char gVertexShader[] =
		"#version 310 es \n"
		"in vec4 vPosition;\n"
		"in vec2 osdtexCoords;\n"
		"in vec2 bgtexCoords;\n"
		"out vec2 osdTexCoords;\n"
		"out vec2 bgTexCoords;\n"
		"void main() {\n"
		"    osdTexCoords = osdtexCoords;\n"
		"    bgTexCoords = bgtexCoords;\n"
		"    gl_Position = vPosition;\n"
		"}\n";

//    "attribute vec4 vPosition;\n"
//    "attribute vec2 texCoords;"
//    "varying vec2 outTexCoords;"

//    "void main() {\n"
//    "    outTexCoords = texCoords;\n"
//    "    gl_Position = vPosition;\n"
//    "}\n";

static const char gFragmentShader[] =
		"#version 310 es \n"
		"#extension GL_OES_EGL_image_external : require \n"
		"#extension GL_EXT_YUV_target : require \n"
		"precision mediump float;\n"
		"uniform __samplerExternal2DY2YEXT osdTexture;\n"
		"uniform __samplerExternal2DY2YEXT bgTexture;\n"
		"yuvCscStandardEXT conv_standard = itu_601;\n"  //itu_601_full_range  itu_709  itu_601
		"in vec2 osdTexCoords;\n"
		"in vec2 bgTexCoords;\n"
		"out vec4 FragColor;\n"
		"void main() {\n"
		"   vec4 osdColor=texture(osdTexture, osdTexCoords);\n"
		"   vec3 osdColor_yuv=rgb_2_yuv(texture(osdTexture, osdTexCoords).xyz,conv_standard);\n"

		"   vec4 bgColor=texture(bgTexture, bgTexCoords);\n"
		"   FragColor=vec4(bgColor.xyz*(1.0-osdColor.a) + osdColor_yuv.xyz*osdColor.a,1.0);\n"
		"}\n";

//    "#extension GL_OES_EGL_image_external : require\n"
//    "precision mediump float;\n"
//    "uniform samplerExternalOES outTexture;"
//    "varying vec2 outTexCoords;"
//    "void main() {\n"
//    //"  gl_FragColor = vec4(0.0, 1.0, 0.0, 1.0);\n"
//    "    gl_FragColor = texture2D(outTexture, outTexCoords);"
//    "}\n";


GLuint loadShader(GLenum shaderType, const char* pSource) {
	GLuint shader = glCreateShader(shaderType);
	if (shader) {
		glShaderSource(shader, 1, &pSource, NULL);
		glCompileShader(shader);
		GLint compiled = 0;
		glGetShaderiv(shader, GL_COMPILE_STATUS, &compiled);
		if (!compiled) {
			GLint infoLen = 0;
			glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &infoLen);
			if (infoLen) {
				char* buf = (char*) malloc(infoLen);
				if (buf) {
					glGetShaderInfoLog(shader, infoLen, NULL, buf);
					fprintf(stderr, "Could not compile shader %d:\n%s\n",
							shaderType, buf);
					free(buf);
				}
				glDeleteShader(shader);
				shader = 0;
			}
		}
	}
	return shader;
}

GLuint createProgram(const char* pVertexSource, const char* pFragmentSource) {
	GLuint vertexShader = loadShader(GL_VERTEX_SHADER, pVertexSource);
	if (!vertexShader) {
		return 0;
	}

	GLuint pixelShader = loadShader(GL_FRAGMENT_SHADER, pFragmentSource);
	if (!pixelShader) {
		return 0;
	}

	GLuint program = glCreateProgram();
	if (program) {
		glAttachShader(program, vertexShader);
		checkGlError("glAttachShader");
		glAttachShader(program, pixelShader);
		checkGlError("glAttachShader");
		glLinkProgram(program);
		GLint linkStatus = GL_FALSE;
		glGetProgramiv(program, GL_LINK_STATUS, &linkStatus);
		if (linkStatus != GL_TRUE) {
			GLint bufLength = 0;
			glGetProgramiv(program, GL_INFO_LOG_LENGTH, &bufLength);
			if (bufLength) {
				char* buf = (char*) malloc(bufLength);
				if (buf) {
					glGetProgramInfoLog(program, bufLength, NULL, buf);
					fprintf(stderr, "Could not link program:\n%s\n", buf);
					free(buf);
				}
			}
			glDeleteProgram(program);
			program = 0;
		}
	}
	return program;
}

GLuint gProgram;
GLuint gvPositionHandle;
GLuint gosdTexCoordsHandle;
GLuint gbgTexCoordsHandle;
GLuint gosdTextureSamplerHandle;
GLuint gbgTextureSamplerHandle;

bool setupGraphics(int w, int h) {
	gProgram = createProgram(gVertexShader, gFragmentShader);
	if (!gProgram) {
		return false;
	}
	gvPositionHandle = glGetAttribLocation(gProgram, "vPosition");
	checkGlError("glGetAttribLocation");
	fprintf(stderr, "glGetAttribLocation(\"vPosition\") = %d\n",
			gvPositionHandle);
	printf("rk-debug[%s %d]  gvPositionHandle:%d \n",__FUNCTION__,__LINE__,gvPositionHandle);

	gosdTexCoordsHandle = glGetAttribLocation(gProgram, "osdtexCoords");
	checkGlError("glGetAttribLocation");
	printf("rk-debug[%s %d]  gosdTexCoordsHandle:%d \n",__FUNCTION__,__LINE__,gosdTexCoordsHandle);

	gbgTexCoordsHandle = glGetAttribLocation(gProgram, "bgtexCoords");
	checkGlError("glGetAttribLocation");
	printf("rk-debug[%s %d]  gbgTexCoordsHandle:%d \n",__FUNCTION__,__LINE__,gbgTexCoordsHandle);

	gosdTextureSamplerHandle = glGetUniformLocation(gProgram, "osdTexture");
	checkGlError("glGetAttribLocation");
	printf("rk-debug[%s %d]  gosdTextureSamplerHandle:%d \n",__FUNCTION__,__LINE__,gosdTextureSamplerHandle);
	gbgTextureSamplerHandle = glGetUniformLocation(gProgram, "bgTexture");
	checkGlError("glGetAttribLocation");
	printf("rk-debug[%s %d]  gbgTextureSamplerHandle:%d \n",__FUNCTION__,__LINE__,gbgTextureSamplerHandle);

	glActiveTexture(GL_TEXTURE0);


	glViewport(0, 0, w, h);
	checkGlError("glViewport");
	return true;
}


//const GLfloat gTriangleVertices[] = {
//    -0.5f, 0.5f,
//    -0.5f, -0.5f,
//    0.5f, -0.5f,
//    0.5f, 0.5f,
//};

//const GLfloat gtexVertices[] = {
//    0.0f, 1.0f,
//    0.0f, 0.0f,
//    1.0f, 0.0f,
//    1.0f, 1.0f,
//};

GLfloat * gTriangleVertices = NULL;

GLfloat * gRGBATexVertices = NULL;
GLfloat * gYUVTexVertices = NULL;

void caculate_Vertex_coordinates(GLfloat * gVertexPoint, float display_w, float display_h, float x,float y,float w,float h)
{
	//归一化至顶点坐标系
	gVertexPoint[0]=(x/display_w)     * 2.0f-1.0f;
	gVertexPoint[1]=(y/display_h)     * 2.0f-1.0f;
	gVertexPoint[2]=(x/display_w)     * 2.0f-1.0f;
	gVertexPoint[3]=((y + h)/display_h) * 2.0f-1.0f;
	gVertexPoint[4]=((x + w)/display_w) * 2.0f-1.0f;
	gVertexPoint[5]=((y + h)/display_h) * 2.0f-1.0f;
	gVertexPoint[6]=((x + w)/display_w) * 2.0f-1.0f;
	gVertexPoint[7]=(y/display_h)     * 2.0f-1.0f;

	for(int i=0;i<4;i++)
		printf("gVertexPoint_%d=(%f,%f) \n",i,gVertexPoint[i*2],gVertexPoint[i*2+1]);
}

void caculate_Texture_coordinates(GLfloat * gTexturePoint, float display_w, float display_h, float x,float y,float w,float h)
{
	//归一化至纹理坐标系
	gTexturePoint[0]=(x/display_w);
	gTexturePoint[1]=(y/display_h);
	gTexturePoint[2]=(x/display_w);
	gTexturePoint[3]=((y + h)/display_h);
	gTexturePoint[4]=((x + w)/display_w);
	gTexturePoint[5]=((y + h)/display_h);
	gTexturePoint[6]=((x + w)/display_w);
	gTexturePoint[7]=(y/display_h);

	for(int i=0;i<4;i++)
		printf("gTexturePoint%d=(%f,%f) \n",i,gTexturePoint[i*2],gTexturePoint[i*2+1]);

}


void renderFrame(rk_texture_t * src_texture,rk_texture_t * dst_texture) {

//        //由于fbo背景,这里不进行clearcolor
//        glClearColor(0.0f, 0.0f, 1.0f, 1.0f);
//        checkGlError("glClearColor");
//        glClear( GL_DEPTH_BUFFER_BIT | GL_COLOR_BUFFER_BIT);
//        checkGlError("glClear");

	glUseProgram(gProgram);
	checkGlError("glUseProgram");

	{
		glVertexAttribPointer(gvPositionHandle, 2, GL_FLOAT, GL_FALSE, 0, gTriangleVertices);
		checkGlError("glVertexAttribPointer");
		glEnableVertexAttribArray(gvPositionHandle);
		checkGlError("glEnableVertexAttribArray");
		glVertexAttribPointer(gosdTexCoordsHandle, 2, GL_FLOAT, GL_FALSE,0, gRGBATexVertices);
		checkGlError("glVertexAttribPointer");
		glEnableVertexAttribArray(gosdTexCoordsHandle);
		checkGlError("glEnableVertexAttribArray");
		glVertexAttribPointer(gbgTexCoordsHandle, 2, GL_FLOAT, GL_FALSE,0, gYUVTexVertices);
		checkGlError("glVertexAttribPointer");
		glEnableVertexAttribArray(gbgTexCoordsHandle);
		checkGlError("glEnableVertexAttribArray");


		glActiveTexture(GL_TEXTURE0);
		glBindTexture(GL_TEXTURE_EXTERNAL_OES, src_texture->texture_id);
		checkGlError("glBindTexture");
		glUniform1i(gosdTextureSamplerHandle, 0);
		checkGlError("glUniform1i");

		glActiveTexture(GL_TEXTURE1);
		glBindTexture(GL_TEXTURE_EXTERNAL_OES, dst_texture->texture_id);
		checkGlError("glBindTexture");
		glUniform1i(gbgTextureSamplerHandle, 1);
		checkGlError("glUniform1i");


		printf("rk-debug[%s %d]  \n",__FUNCTION__,__LINE__);
		glDrawArrays(GL_TRIANGLE_FAN, 0, 4);
		checkGlError("glDrawArrays");

	}

}



static void checkEglError(const char* op, EGLBoolean returnVal = EGL_TRUE) {
	if (returnVal != EGL_TRUE) {
		fprintf(stderr, "%s() returned %d\n", op, returnVal);
	}

	for (EGLint error = eglGetError(); error != EGL_SUCCESS; error
																	 = eglGetError()) {
		fprintf(stderr, "after %s() eglError (0x%x)\n", op,error);
	}
}


int read_img_from_file(void * buffer,const char* file_path, int rw, int rh, int vw, float format_size)
{
	FILE * pfile = NULL;
	char layername[100] ;
	sprintf(layername,"%s",file_path);

	pfile = fopen(layername,"rb");
	if(pfile)
	{
		if(rw == vw)   //实宽虚宽相等,一次性读入.
		{
			//int size =src.w*src.h*(src.is_afbc?2:1)* get_format_size(src.drm_format);
			int size = rw*rh*format_size;
			int fret= fread((void *)buffer,1,size,pfile);
			LOGCATD("rk-debug read %s Success size:%d fread_size=%d\n",layername,size,fret);

		}else {    //实宽虚宽不等,逐行读取.
			for(int i = 0;i < rh;i++)
			{
				int stride = vw * format_size;
				char * temp_addr = (char *)buffer + i * stride;
				fseek (pfile, i * rw * format_size, SEEK_SET);
				int fret= fread((void *)temp_addr,1,rw * format_size,pfile);
				LOGCATD("rk-debug read %s Success fread_size=%d\n",layername,fret);
			}
		}

		fclose(pfile);
	}else{
		LOGCATD("rk-debug Could not open file:%s !\n",layername);
		return -1;
	}
	return 0;
}


int dumpPixels_new(int index,int inWindowWidth,int inWindowHeight,void * pPixelDataFront,const char * format,int size){
	char file_name[100];
	sprintf(file_name,"/data/dump/dumplayer_%d_%dx%d_%s.bin",index,inWindowWidth,inWindowHeight,format);
	if(1)
	{
		FILE *file = fopen(file_name, "wb");
		if (!file)
		{
			printf("Could not open /%s \n",file_name);
			return -1;
		} else {
			printf("open %s and write ok\n",file_name);
		}
		fwrite(pPixelDataFront, size, 1, file);
		fclose(file);
	}

	return 0;
}


void *alloc_drm_buf(int *fd,int in_w, int in_h, int in_bpp)
{
	printf("rk-debug[%s %d] *fd:%d w:%d h:%d in_bpp:%d \n",__FUNCTION__,__LINE__,*fd,in_w,in_h,in_bpp);

	static const char* card = "/dev/dri/card0";
	int drm_fd = -1;
	int flag = O_RDWR;
	int ret;
	void *map = NULL;

	void *vir_addr = NULL;
	struct drm_prime_handle fd_args;
	struct drm_mode_map_dumb mmap_arg;
	struct drm_mode_destroy_dumb destory_arg;

	struct drm_mode_create_dumb alloc_arg;

	drm_fd = open(card, flag);
	if(drm_fd < 0)
	{
		printf("failed to open %s\n", card);
		return NULL;
	}

	memset(&alloc_arg, 0, sizeof(alloc_arg));
	alloc_arg.bpp = in_bpp;
	alloc_arg.width = in_w;
	alloc_arg.height = in_h;

	ret = drmIoctl(drm_fd, DRM_IOCTL_MODE_CREATE_DUMB, &alloc_arg);
	if (ret) {
		printf("failed to create dumb buffer: %s\n", strerror(errno));
		return NULL;
	}

	memset(&fd_args, 0, sizeof(fd_args));
	fd_args.fd = -1;
	fd_args.handle = alloc_arg.handle;;
	fd_args.flags = 0;
	ret = drmIoctl(drm_fd, DRM_IOCTL_PRIME_HANDLE_TO_FD, &fd_args);
	if (ret)
	{
		printf("rk-debug handle_to_fd failed ret=%d,err=%s, handle=%x \n",ret,strerror(errno),fd_args.handle);
		return NULL;
	}
	printf("Dump fd = %d \n",fd_args.fd);
	*fd = fd_args.fd;

	//handle to Virtual address
	memset(&mmap_arg, 0, sizeof(mmap_arg));
	mmap_arg.handle = alloc_arg.handle;

	ret = drmIoctl(drm_fd, DRM_IOCTL_MODE_MAP_DUMB, &mmap_arg);
	if (ret) {
		printf("failed to create map dumb: %s\n", strerror(errno));
		vir_addr = NULL;
		goto destory_dumb;
	}
	vir_addr = map = mmap64(0, alloc_arg.size, PROT_READ | PROT_WRITE, MAP_SHARED, drm_fd, mmap_arg.offset);
	if (map == MAP_FAILED) {
		printf("failed to mmap buffer: %s\n", strerror(errno));
		vir_addr = NULL;
		goto destory_dumb;
	}
	printf("alloc map=%p \n",map);
	close(drm_fd);
	return vir_addr;
destory_dumb:
	memset(&destory_arg, 0, sizeof(destory_arg));
	destory_arg.handle = alloc_arg.handle;
	int fdd = *fd ;
	ret = drmIoctl(fdd, DRM_IOCTL_MODE_DESTROY_DUMB, &destory_arg);
	if (ret)
		printf("failed to destory dumb %d\n", ret);
	return vir_addr;
}

EGLDisplay initEGLContex()
{

	EGLBoolean returnValue;
	EGLConfig myConfig = {0};

	EGLint context_attribs[] = { EGL_CONTEXT_CLIENT_VERSION, 2, EGL_NONE };
	EGLint s_configAttribs[] = {
			EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
			EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
			EGL_NONE };

	EGLint majorVersion;
	EGLint minorVersion;
	EGLContext context;
	EGLSurface surface;
	EGLint w, h;

	EGLDisplay dpy;

	checkEglError("<init>");
	dpy = eglGetDisplay(EGL_DEFAULT_DISPLAY);
	checkEglError("eglGetDisplay");
	if (dpy == EGL_NO_DISPLAY) {
		printf("eglGetDisplay returned EGL_NO_DISPLAY.\n");
		return 0;
	}

	returnValue = eglInitialize(dpy, &majorVersion, &minorVersion);
	checkEglError("eglInitialize", returnValue);
	fprintf(stderr, "EGL version %d.%d\n", majorVersion, minorVersion);
	if (returnValue != EGL_TRUE) {
		printf("eglInitialize failed\n");
		return 0;
	}


	EGLint numConfig = 0;
	eglChooseConfig(dpy, s_configAttribs, 0, 0, &numConfig);
	int num = numConfig;
	if(num != 0){
		EGLConfig configs[num];
		//获取所有满足attributes的configs
		eglChooseConfig(dpy, s_configAttribs, configs, num, &numConfig);
		myConfig = configs[0]; //以某种规则选择一个config，这里使用了最简单的规则。
	}

	int sw = 100;
	int sh = 100;
	EGLint attribs[] = { EGL_WIDTH, sw, EGL_HEIGHT, sh, EGL_LARGEST_PBUFFER, EGL_TRUE, EGL_NONE, EGL_NONE };
	surface = eglCreatePbufferSurface(dpy, myConfig, attribs);

	checkEglError("eglCreateWindowSurface");
	if (surface == EGL_NO_SURFACE) {
		printf("eglCreateWindowSurface failed.\n");
		return 0;
	}

	context = eglCreateContext(dpy, myConfig, EGL_NO_CONTEXT, context_attribs);
	checkEglError("eglCreateContext");
	if (context == EGL_NO_CONTEXT) {
		printf("eglCreateContext failed\n");
		return 0;
	}
	returnValue = eglMakeCurrent(dpy, surface, surface, context);
	checkEglError("eglMakeCurrent", returnValue);
	if (returnValue != EGL_TRUE) {
		return 0;
	}
	eglQuerySurface(dpy, surface, EGL_WIDTH, &w);
	checkEglError("eglQuerySurface");
	eglQuerySurface(dpy, surface, EGL_HEIGHT, &h);
	checkEglError("eglQuerySurface");

	fprintf(stderr, "Window dimensions: %d x %d\n", w, h);

	printGLString("Version", GL_VERSION);
	printGLString("Vendor", GL_VENDOR);
	printGLString("Renderer", GL_RENDERER);
	printGLString("Extensions", GL_EXTENSIONS);

	return dpy;

}

#ifndef DRM_FORMAT_YUV420_8BIT
#define DRM_FORMAT_YUV420_8BIT  fourcc_code('Y', 'U', '0', '8')
#endif

#ifndef DRM_FORMAT_YUV420_10BIT
#define DRM_FORMAT_YUV420_10BIT fourcc_code('Y', 'U', '1', '0')
#endif

#ifndef DRM_FORMAT_YUYV   //YUV422 1plane
#define DRM_FORMAT_YUYV         fourcc_code('Y', 'U', 'Y', 'V')
#endif

#ifndef DRM_FORMAT_Y210  //YUV422 10bit 1plane
#define DRM_FORMAT_Y210         fourcc_code('Y', '2', '1', '0')
#endif


int dump_rk_texture(rk_texture_t * rk_texture)
{
	if(!rk_texture){
		printf("dump error!!! rk_texture = NULL\n");
		return -1;
	}

	printf("rk_texture{ w:%d h:%d format:0x%x afbc:%d texture_id:%d need_fbo:%d fbo_id:%d drm_fd:%d drm_viraddr:%p } \n"
			,rk_texture->w,rk_texture->h,rk_texture->drm_format,rk_texture->is_afbc,rk_texture->texture_id
			,rk_texture->need_fbo,rk_texture->fbo_id,rk_texture->drm_fd,rk_texture->drm_viraddr);
	return 0;
}


float get_format_size(int in_format)
{
	//create img
	switch(in_format){
		case DRM_FORMAT_ABGR8888:
			return 4.0;
			break;
		case DRM_FORMAT_BGR888:
		case DRM_FORMAT_RGB888:
			return 3.0;
			break;
		case DRM_FORMAT_RGBA5551:
			return 2.0;
			break;
		case DRM_FORMAT_YUYV:
			return 2.0;
			break;
		case DRM_FORMAT_NV12:
		case DRM_FORMAT_YUV420_8BIT:
			return 1.5;
			break;
		default :
			return 0;
	}

}


int create_drm_fd(rk_texture_t * rk_texture)
{
	int in_format = rk_texture->drm_format;
	int is_afbc = rk_texture->is_afbc;
	int textureW = rk_texture->w;
	int textureH = rk_texture->h;


	//create img
	switch(in_format){
		case DRM_FORMAT_ABGR8888:
			rk_texture->drm_viraddr = alloc_drm_buf(&(rk_texture->drm_fd),textureW,textureH,is_afbc?64:32); //afbc bpp 先按照2倍申请
			break;
		case DRM_FORMAT_BGR888:
		case DRM_FORMAT_RGB888:
			rk_texture->drm_viraddr = alloc_drm_buf(&(rk_texture->drm_fd),textureW,textureH,is_afbc?48:24); //afbc bpp 先按照2倍申请
			break;
		case DRM_FORMAT_RGBA5551:
			rk_texture->drm_viraddr = alloc_drm_buf(&(rk_texture->drm_fd),ALIGN(textureW,16),textureH,16); //无afbc ,16bit=2Byte
			break;
		case DRM_FORMAT_YUYV:
			rk_texture->drm_viraddr = alloc_drm_buf(&(rk_texture->drm_fd),textureW,textureH,is_afbc?32:16); //afbc bpp 先按照2倍申请
			break;
		case DRM_FORMAT_NV12:
		case DRM_FORMAT_YUV420_8BIT:

			rk_texture->drm_viraddr = alloc_drm_buf(&(rk_texture->drm_fd),textureW,textureH,is_afbc?24:12); //afbc bpp 先按照2倍申请
			break;
	}
	return 0;
}



int create_texture_fbo_img(EGLDisplay dpy,rk_texture_t * rk_texture)
{

	EGLImageKHR img = NULL;
	PFNGLEGLIMAGETARGETTEXTURE2DOESPROC image_target_texture_2d;
	PFNEGLCREATEIMAGEKHRPROC create_image;
	PFNEGLDESTROYIMAGEKHRPROC destroy_image;
	create_image = (PFNEGLCREATEIMAGEKHRPROC) eglGetProcAddress("eglCreateImageKHR");
	image_target_texture_2d = (PFNGLEGLIMAGETARGETTEXTURE2DOESPROC) eglGetProcAddress("glEGLImageTargetTexture2DOES");
	destroy_image = (PFNEGLDESTROYIMAGEKHRPROC) eglGetProcAddress("eglDestroyImageKHR");

	int in_format = rk_texture->drm_format;
	int is_afbc = rk_texture->is_afbc;
	GLuint * p_texture_id =(GLuint *) &(rk_texture->texture_id);
	GLuint * p_fbo_id = (GLuint *)&(rk_texture->fbo_id);
	int fd = rk_texture->drm_fd;
	int textureW = rk_texture->w;
	int textureH = rk_texture->h;
	int is_need_fbo = rk_texture->need_fbo;

	LOGCATD("rk-debug[%s %d] rk_texture:%p t:%p p_texture_id:%p \n",__FUNCTION__,__LINE__,rk_texture,&(rk_texture->texture_id),p_texture_id);


	if(dump_rk_texture(rk_texture))
	{
		LOGCATD("rk-debug[%s %d] rk_texture == NULL \n",__FUNCTION__,__LINE__);
	}

	//create img
	switch(in_format){
		case DRM_FORMAT_ABGR8888:
		{
			int stride = ALIGN(textureW, 32) * 4;

			EGLint attr[] = {
					EGL_WIDTH, textureW,
					EGL_HEIGHT, textureH,
					EGL_LINUX_DRM_FOURCC_EXT, DRM_FORMAT_ABGR8888,
					EGL_DMA_BUF_PLANE0_FD_EXT, fd,
					EGL_DMA_BUF_PLANE0_OFFSET_EXT, 0,
					EGL_DMA_BUF_PLANE0_PITCH_EXT, stride,
					is_afbc?EGL_DMA_BUF_PLANE0_MODIFIER_LO_EXT:EGL_NONE, static_cast<EGLint>(is_afbc?((AFBC_FORMAT_MOD_SPARSE | AFBC_FORMAT_MOD_BLOCK_SIZE_16x16)&0xffffffff):EGL_NONE), //rk支持afbc默认格式
					is_afbc?EGL_DMA_BUF_PLANE0_MODIFIER_HI_EXT:EGL_NONE, is_afbc?(0x08<<24):EGL_NONE,  //ARM平台标志位
					EGL_NONE
			};
			img = create_image(dpy, EGL_NO_CONTEXT,EGL_LINUX_DMA_BUF_EXT, (EGLClientBuffer)NULL, attr);
			ECHK(img);
			if(img == EGL_NO_IMAGE_KHR)
			{
				LOGCATD("rk-debug eglCreateImageKHR NULL \n ");
				return -1;
			}
		}
			break;
		case DRM_FORMAT_BGR888:
		case DRM_FORMAT_RGB888:
		{
			int stride = ALIGN(textureW, 32) * 3;

			EGLint attr[] = {
					EGL_WIDTH, textureW,
					EGL_HEIGHT, textureH,
					EGL_LINUX_DRM_FOURCC_EXT, in_format,
					EGL_DMA_BUF_PLANE0_FD_EXT, fd,
					EGL_DMA_BUF_PLANE0_OFFSET_EXT, 0,
					EGL_DMA_BUF_PLANE0_PITCH_EXT, stride,
					is_afbc?EGL_DMA_BUF_PLANE0_MODIFIER_LO_EXT:EGL_NONE, static_cast<EGLint>(is_afbc?((AFBC_FORMAT_MOD_SPARSE | AFBC_FORMAT_MOD_BLOCK_SIZE_16x16)&0xffffffff):EGL_NONE), //rk支持afbc默认格式
					is_afbc?EGL_DMA_BUF_PLANE0_MODIFIER_HI_EXT:EGL_NONE, is_afbc?(0x08<<24):EGL_NONE,  //ARM平台标志位
					EGL_NONE
			};
			img = create_image(dpy, EGL_NO_CONTEXT,EGL_LINUX_DMA_BUF_EXT, (EGLClientBuffer)NULL, attr);
			ECHK(img);
			if(img == EGL_NO_IMAGE_KHR)
			{
				LOGCATE("rk-debug eglCreateImageKHR NULL \n ");
				return -1;
			}
		}
			break;


		case DRM_FORMAT_RGBA5551:
		{
			int stride = ALIGN(textureW, 16) * 2;  //stride 16对齐后 * 2Byte
			//int stride = textureW;
			EGLint attr[] = {
					EGL_WIDTH, textureW,
					EGL_HEIGHT, textureH,
					EGL_LINUX_DRM_FOURCC_EXT, DRM_FORMAT_RGBA5551,
					EGL_DMA_BUF_PLANE0_FD_EXT, fd,
					EGL_DMA_BUF_PLANE0_OFFSET_EXT, 0,
					EGL_DMA_BUF_PLANE0_PITCH_EXT, stride,
					is_afbc?EGL_DMA_BUF_PLANE0_MODIFIER_LO_EXT:EGL_NONE, static_cast<EGLint>(is_afbc?((AFBC_FORMAT_MOD_SPARSE | AFBC_FORMAT_MOD_BLOCK_SIZE_16x16)&0xffffffff):EGL_NONE), //rk支持afbc默认格式
					is_afbc?EGL_DMA_BUF_PLANE0_MODIFIER_HI_EXT:EGL_NONE, is_afbc?(0x08<<24):EGL_NONE,  //ARM平台标志位
					EGL_NONE
			};
			img = create_image(dpy, EGL_NO_CONTEXT,EGL_LINUX_DMA_BUF_EXT, (EGLClientBuffer)NULL, attr);
			ECHK(img);
			if(img == EGL_NO_IMAGE_KHR)
			{
				LOGCATE("rk-debug eglCreateImageKHR NULL \n ");
				return -1;
			}
		}
			break;
		case DRM_FORMAT_YUYV:
		{
			int stride = ALIGN(textureW, 32) * 2;

			EGLint attr[] = {
					EGL_WIDTH, textureW,
					EGL_HEIGHT, textureH,
					EGL_LINUX_DRM_FOURCC_EXT, DRM_FORMAT_YUYV,
					EGL_DMA_BUF_PLANE0_FD_EXT, fd,
					EGL_DMA_BUF_PLANE0_OFFSET_EXT, 0,
					EGL_DMA_BUF_PLANE0_PITCH_EXT, stride,
					is_afbc?EGL_DMA_BUF_PLANE0_MODIFIER_LO_EXT:EGL_NONE, static_cast<EGLint>(is_afbc?((AFBC_FORMAT_MOD_SPARSE | AFBC_FORMAT_MOD_BLOCK_SIZE_16x16)&0xffffffff):EGL_NONE), //rk支持afbc默认格式
					is_afbc?EGL_DMA_BUF_PLANE0_MODIFIER_HI_EXT:EGL_NONE, is_afbc?(0x08<<24):EGL_NONE,  //ARM平台标志位
					EGL_NONE
			};
			img = create_image(dpy, EGL_NO_CONTEXT,EGL_LINUX_DMA_BUF_EXT, (EGLClientBuffer)NULL, attr);
			ECHK(img);
			if(img == EGL_NO_IMAGE_KHR)
			{
				LOGCATE("rk-debug eglCreateImageKHR NULL \n ");
				return -1;
			}
		}
			break;
		case DRM_FORMAT_YUV420_8BIT: //该格式仅支持afbc，不支持linear
		{
			int stride = ALIGN(textureW, 32) * 1;

			EGLint attr[] = {
					EGL_WIDTH, textureW,
					EGL_HEIGHT, textureH,
					EGL_LINUX_DRM_FOURCC_EXT, DRM_FORMAT_YUV420_8BIT,
					EGL_DMA_BUF_PLANE0_FD_EXT, fd,
					EGL_DMA_BUF_PLANE0_OFFSET_EXT, 0,
					EGL_DMA_BUF_PLANE0_PITCH_EXT, stride, //该格式afbc 无所谓stride 为1还是2
					is_afbc?EGL_DMA_BUF_PLANE0_MODIFIER_LO_EXT:EGL_NONE, static_cast<EGLint>(is_afbc?((AFBC_FORMAT_MOD_SPARSE | AFBC_FORMAT_MOD_BLOCK_SIZE_16x16)&0xffffffff):EGL_NONE), //rk支持afbc默认格式
					is_afbc?EGL_DMA_BUF_PLANE0_MODIFIER_HI_EXT:EGL_NONE, is_afbc?(0x08<<24):EGL_NONE,  //ARM平台标志位
					EGL_NONE
			};
			img = create_image(dpy, EGL_NO_CONTEXT,EGL_LINUX_DMA_BUF_EXT, (EGLClientBuffer)NULL, attr);
			ECHK(img);
			if(img == EGL_NO_IMAGE_KHR)
			{
				LOGCATE("rk-debug eglCreateImageKHR NULL \n ");
				return -1;
			}
		}
			break;

		case DRM_FORMAT_NV12:
		{
			int stride = ALIGN(textureW, 32) * 1;
			if(!is_afbc)
			{
				EGLint attr[] = {
						EGL_WIDTH, textureW,
						EGL_HEIGHT, textureH,
						EGL_LINUX_DRM_FOURCC_EXT, DRM_FORMAT_NV12,
						EGL_DMA_BUF_PLANE0_FD_EXT, fd,
						EGL_DMA_BUF_PLANE0_OFFSET_EXT, 0,
						EGL_DMA_BUF_PLANE0_PITCH_EXT, stride,
						EGL_DMA_BUF_PLANE1_FD_EXT, fd,
						EGL_DMA_BUF_PLANE1_OFFSET_EXT, stride*textureH,
						EGL_DMA_BUF_PLANE1_PITCH_EXT, stride,
						EGL_NONE
				};
				img = create_image(dpy, EGL_NO_CONTEXT,EGL_LINUX_DMA_BUF_EXT, (EGLClientBuffer)NULL, attr);
				ECHK(img);
				if(img == EGL_NO_IMAGE_KHR)
				{
					LOGCATE("rk-debug eglCreateImageKHR NULL \n ");
					return -1;
				}


			}else {
				EGLint attr[] = {
						EGL_WIDTH, textureW,
						EGL_HEIGHT, textureH,
						EGL_LINUX_DRM_FOURCC_EXT, DRM_FORMAT_NV12,

						EGL_DMA_BUF_PLANE0_FD_EXT, fd,
						EGL_DMA_BUF_PLANE0_OFFSET_EXT, 0,
						EGL_DMA_BUF_PLANE0_PITCH_EXT, stride,
						EGL_DMA_BUF_PLANE0_MODIFIER_LO_EXT, ((AFBC_FORMAT_MOD_SPARSE | AFBC_FORMAT_MOD_BLOCK_SIZE_32x8)&0xffffffff), //rk支持afbc默认格式
						EGL_DMA_BUF_PLANE0_MODIFIER_HI_EXT, (0x08<<24),  //ARM平台标志位

						EGL_DMA_BUF_PLANE1_FD_EXT, fd,
						EGL_DMA_BUF_PLANE1_OFFSET_EXT, stride*textureH,
						EGL_DMA_BUF_PLANE1_PITCH_EXT, stride,
						EGL_DMA_BUF_PLANE1_MODIFIER_LO_EXT, ((AFBC_FORMAT_MOD_SPARSE | AFBC_FORMAT_MOD_BLOCK_SIZE_64x4)&0xffffffff), //rk支持afbc默认格式
						EGL_DMA_BUF_PLANE1_MODIFIER_HI_EXT, (0x08<<24),  //ARM平台标志位

						EGL_NONE
				};
				img = create_image(dpy, EGL_NO_CONTEXT,EGL_LINUX_DMA_BUF_EXT, (EGLClientBuffer)NULL, attr);
				ECHK(img);
				if(img == EGL_NO_IMAGE_KHR)
				{
					LOGCATE("rk-debug eglCreateImageKHR NULL \n ");
					return -1;
				}
				LOGCATD("rk-debug[%s %d] nv12 afbc is fault!\n",__FUNCTION__,__LINE__);
			}
		}
			break;
		default:
			LOGCATE("rk-debug[%s %d] error in_format unSupport:0x%x \n",__FUNCTION__,__LINE__,in_format);

	}


	if(!is_need_fbo)
	{
		GCHK(glActiveTexture(GL_TEXTURE0));
		GCHK(glGenTextures(1, p_texture_id));
		printf("rk-debug[%s %d] p_texture_id:%d \n",__FUNCTION__,__LINE__,*p_texture_id);
		GCHK(glBindTexture(GL_TEXTURE_EXTERNAL_OES, *p_texture_id));
		GCHK(glTexParameteri(GL_TEXTURE_EXTERNAL_OES, GL_TEXTURE_MAG_FILTER, GL_NEAREST));
		GCHK(glTexParameteri(GL_TEXTURE_EXTERNAL_OES, GL_TEXTURE_MIN_FILTER, GL_NEAREST));
		GCHK(image_target_texture_2d(GL_TEXTURE_EXTERNAL_OES, img));

	}else{
		GCHK(glActiveTexture(GL_TEXTURE1));
		GCHK(glGenTextures(1, p_texture_id));
		printf("rk-debug[%s %d] p_texture_id:%d \n",__FUNCTION__,__LINE__,*p_texture_id);
		GCHK(glBindTexture(GL_TEXTURE_EXTERNAL_OES, *p_texture_id));
		GCHK(glTexParameteri(GL_TEXTURE_EXTERNAL_OES, GL_TEXTURE_MAG_FILTER, GL_NEAREST));
		GCHK(glTexParameteri(GL_TEXTURE_EXTERNAL_OES, GL_TEXTURE_MIN_FILTER, GL_NEAREST));
		GCHK(image_target_texture_2d(GL_TEXTURE_EXTERNAL_OES, img));

		glGenFramebuffers(1, p_fbo_id);
		glBindFramebuffer(GL_FRAMEBUFFER, *p_fbo_id);
		printf("rk-debug[%s %d] p_fbo_id:%d \n",__FUNCTION__,__LINE__,*p_fbo_id);
		GCHK(glFramebufferTexture2D(GL_FRAMEBUFFER,
									GL_COLOR_ATTACHMENT0, GL_TEXTURE_EXTERNAL_OES, *p_texture_id, 0));
		if(glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE)
			LOGCATD("rk_debug create fbo success!\n");
		else
			LOGCATE("rk_debug create fbo failed!\n");
	}

	destroy_image(dpy,img);

	if(dump_rk_texture(rk_texture))
	{
		LOGCATE("rk-debug[%s %d] rk_texture == NULL \n",__FUNCTION__,__LINE__);
	}


	return 0;
}

int destory_texture_fbo_img(EGLDisplay dpy,rk_texture_t * rk_texture)
{
	GLuint * p_texture_id = (GLuint *) &(rk_texture->texture_id);
	GLuint * p_fbo_id = (GLuint *) &(rk_texture->fbo_id);
	int is_need_fbo = rk_texture->need_fbo;

	glDeleteTextures(1,p_texture_id);

	if(is_need_fbo)
		glDeleteFramebuffers(1,p_fbo_id);

	printf("rk-debug[%s %d] delete tex:%d fbo:%d\n",__FUNCTION__,__LINE__,*p_texture_id,*p_fbo_id);

	return 0;
}


/*
  * Class:     com_hikvision_jni_MyCam
  * Method:    startPreview
  * Signature: (Landroid/view/SurfaceHolder;)V
  */
 JNIEXPORT void JNICALL helloWorld(JNIEnv *env, jobject instance)
 {
     LOGCATE("helloworld");
	 EGLDisplay dpy = initEGLContex();

	 rk_texture_t src={0};
	 rk_texture_t win={0};

	 //init src param
	 src.w = 600;  //DRM_FORMAT_RGBA5551格式，申请buffer时要16对齐,配置create_image的attr时,stride也得16对齐，即虚宽是608，实宽600. 纹理坐标正常按照600使用.
	 src.h = 48;
	 src.need_fbo = 0;
	 src.drm_format = DRM_FORMAT_RGBA5551; //DRM_FORMAT_NV12, DRM_FORMAT_YUYV, DRM_FORMAT_ABGR8888,DRM_FORMAT_YUV420_8BIT(afbc)
	 src.is_afbc = 0;

	 //init win param
	 win.w = 1920;
	 win.h = 1080;
	 win.need_fbo = 1;
	 win.drm_format = DRM_FORMAT_NV12; //DRM_FORMAT_NV12, DRM_FORMAT_YUYV, DRM_FORMAT_ABGR8888, DRM_FORMAT_BGR888, DRM_FORMAT_RGB888,
	 win.is_afbc = 0;

	 create_drm_fd(&src);
	 create_drm_fd(&win);

	 read_img_from_file(src.drm_viraddr,"/data/600_48_5551.rgba", src.w, src.h, ALIGN(src.w,16),get_format_size(src.drm_format));
	 read_img_from_file(win.drm_viraddr,"/data/Capt_Chn0_1920x1080.yuv", win.w, win.h, ALIGN(win.w,16),get_format_size(win.drm_format));


	 create_texture_fbo_img(dpy, &src);
	 create_texture_fbo_img(dpy, &win);


	 int w = win.w;
	 int h = win.h;


	 gTriangleVertices = (GLfloat *)malloc(4*sizeof(GLfloat)*2);
	 gRGBATexVertices = (GLfloat *)malloc(4*sizeof(GLfloat)*2);
	 gYUVTexVertices = (GLfloat *)malloc(4*sizeof(GLfloat)*2);


	 caculate_Vertex_coordinates(gTriangleVertices,win.w,win.h,100,200,src.w,src.h);
	 caculate_Texture_coordinates(gRGBATexVertices,src.w,src.h,0,0,src.w,src.h);
	 caculate_Texture_coordinates(gYUVTexVertices,win.w,win.h,100,200,src.w,src.h);



	 if(!setupGraphics(w, h)) {
		 LOGCATE("Could not set up graphics.\n");
	 }


	 struct timeval tpend1, tpend2;
	 float usec1 = 0;


	 for (int i = 0; i < 1; i++) {
		 gettimeofday(&tpend1, NULL);

//create_texture_fbo_img(dpy, &src);

//        if(i%2 == 1) {
		 renderFrame(&src,&win);
		 glFinish();
//        }else {
//            glClearColor(0.0f, 0.0f, 1.0f, 1.0f);
//            checkGlError("glClearColor");
//            glClear( GL_DEPTH_BUFFER_BIT | GL_COLOR_BUFFER_BIT);
//            checkGlError("glClear");
//            glFinish();
//        }

//destory_texture_fbo_img(dpy, &src);

		 gettimeofday(&tpend2, NULL);
		 usec1 = 1000.0 * (tpend2.tv_sec - tpend1.tv_sec) + (tpend2.tv_usec - tpend1.tv_usec) / 1000.0;
		 printf("rk-debug[%s %d]  renderFrame i:%d use time=%f ms\n",__FUNCTION__,__LINE__,i,usec1);

		 if(i%1000 == 0&&i>10000){
			 sleep(5);
		 }

	 }

	 glFinish();
//    while(1){}

	 //dump result
	 switch(win.drm_format){
		 case DRM_FORMAT_ABGR8888:
			 if(win.is_afbc)
				 dumpPixels_new(1,w,h,win.drm_viraddr,"ABGR8888_afbc",w*h*8);
			 else
				 dumpPixels_new(1,w,h,win.drm_viraddr,"ABGR8888",w*h*4);
			 break;
		 case DRM_FORMAT_BGR888:
			 if(win.is_afbc)
				 dumpPixels_new(1,w,h,win.drm_viraddr,"BGR888_afbc",w*h*6);
			 else
				 dumpPixels_new(1,w,h,win.drm_viraddr,"BGR888",w*h*3);
			 break;
		 case DRM_FORMAT_RGB888:
			 if(win.is_afbc)
				 dumpPixels_new(1,w,h,win.drm_viraddr,"RGB888_afbc",w*h*6);
			 else
				 dumpPixels_new(1,w,h,win.drm_viraddr,"RGB888",w*h*3);
			 break;

		 case DRM_FORMAT_YUYV:
			 if(win.is_afbc)
				 dumpPixels_new(1,w,h,win.drm_viraddr,"YUYV_afbc",w*h*4);
			 else
				 dumpPixels_new(1,w,h,win.drm_viraddr,"YUYV",w*h*2);
			 break;

		 case DRM_FORMAT_NV12:
			 if(win.is_afbc)
				 dumpPixels_new(1,w,h,win.drm_viraddr,"nv12_afbc",w*h*3);
			 else
				 dumpPixels_new(1,w,h,win.drm_viraddr,"nv12",w*h*3/2);
			 break;

		 case DRM_FORMAT_YUV420_8BIT:
			 if(win.is_afbc)
				 dumpPixels_new(1,w,h,win.drm_viraddr,"YUV420I_afbc",w*h*3);
			 else
				 dumpPixels_new(1,w,h,win.drm_viraddr,"YUV420I",w*h*3/2);
			 break;

		 default :
			 printf("rk-debug[%s %d] unsupport format:0x%x \n",__FUNCTION__,__LINE__,win.drm_format);

	 }



#if 0 //read rgba pixel
	 char * pPixelDataFront = NULL;
    pPixelDataFront = (char*)malloc(w*h*4);
    glReadPixels(0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, pPixelDataFront);
    dumpPixels(1,w,h,pPixelDataFront);
#endif
 }




#ifdef __cplusplus
}
#endif

//视频预览相关
static JNINativeMethod g_RenderMethods[] = {
		{"native_helloWorld",                       "()V",                             (void *)(helloWorld)},

};

//mediacodec相关
static JNINativeMethod g_AudioMethods[] = {



};


static int RegisterNativeMethods(JNIEnv *env, const char *className, JNINativeMethod *methods, int methodNum)
{
	LOGCATE("RegisterNativeMethods");
	jclass clazz = env->FindClass(className);
	if (clazz == NULL)
	{
		LOGCATE("RegisterNativeMethods fail. clazz == NULL");
		return JNI_FALSE;
	}
	if (env->RegisterNatives(clazz, methods, methodNum) < 0)
	{
		LOGCATE("RegisterNativeMethods fail");
		return JNI_FALSE;
	}
	return JNI_TRUE;
}

static void UnregisterNativeMethods(JNIEnv *env, const char *className)
{
	LOGCATE("UnregisterNativeMethods");
	jclass clazz = env->FindClass(className);
	if (clazz == NULL)
	{
		LOGCATE("UnregisterNativeMethods fail. clazz == NULL");
		return;
	}
	if (env != NULL)
	{
		env->UnregisterNatives(clazz);
	}
}

// call this func when loading lib
extern "C" jint JNI_OnLoad(JavaVM *jvm, void *p)
{
	LOGCATE("===== JNI_OnLoad =====");
	jint jniRet = JNI_ERR;
	JNIEnv *env = NULL;
	if (jvm->GetEnv((void **) (&env), JNI_VERSION_1_6) != JNI_OK)
	{
		return jniRet;
	}
	//视频预览相关操作
	jint regRet = RegisterNativeMethods(env, NATIVE_TEST_CLASS_NAME, g_RenderMethods,
										sizeof(g_RenderMethods) /
										sizeof(g_RenderMethods[0]));
	if (regRet != JNI_TRUE)
	{
		return JNI_ERR;
	}

	return JNI_VERSION_1_6;
}

extern "C" void JNI_OnUnload(JavaVM *jvm, void *p)
{
	JNIEnv *env = NULL;
	if (jvm->GetEnv((void **) (&env), JNI_VERSION_1_6) != JNI_OK)
	{
		return;
	}
	UnregisterNativeMethods(env, NATIVE_TEST_CLASS_NAME);
}
