/*
 * Copyright 1993-2010 NVIDIA Corporation.  All rights reserved.
 *
 * Please refer to the NVIDIA end user license agreement (EULA) associated
 * with this source code for terms and conditions that govern your use of
 * this software. Any use, reproduction, disclosure, or distribution of
 * this software and related documentation outside the terms of the EULA
 * is strictly prohibited.
 *
 */


#include <GL/glew.h>

// CUDA utilities and system includes
#include <cutil_inline.h>    // includes cuda.h and cuda_runtime_api.h
#include <cutil_gl_inline.h> // includes cuda_gl_interop.h

// Includes
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>


#include <SDL.h>
//#include <SDL_opengl.h>

// shader for displaying floating-point texture
static const char *shader_code = 
"!!ARBfp1.0\n"
"TEX result.color, fragment.texcoord, texture[0], 2D; \n"
"END";

////////////////////////////////////////////////////////////////////////////////
// Global data handlers and parameters
////////////////////////////////////////////////////////////////////////////////
//OpenGL PBO and texture "names"
GLuint gl_PBO, gl_Tex;
struct cudaGraphicsResource *cuda_pbo_resource; // handles OpenGL-CUDA exchange
GLuint shader;

extern "C"{
#include "aes_gpu.h"
#include "encrypted-display-backend.h"
#include "encrypted-display.h"
}

#define BUFFER_DATA(i) ((char *)0 + i)

#include <pthread.h>
#include "Thread.hxx"
#include "Lock.hxx"
#include <assert.h>


extern "C" void X11_GL_SwapBuffers_mark();
extern "C" int X11_GL_CreateContext_mark();
// THIS IS THE MAIN FUNCTION INITIALIZING GL
static int init_gl(void)
{
 if ( X11_GL_CreateContext_mark() < 0 ) {
             return(-1);
  }
        return 0;

}




struct exchange_buffer{
	uchar4* bufptr;
	int w; int h;
	uchar* aeskeyptr;
	struct bigint iv;
	
	Lock lock;
	bool ready_cpu;
	bool ready_gpu;
	bool inited;
	double frameRate;
	long frameRateCount;

	void draw();
	void invoke_thread(uchar* aeskey, struct bigint iv, int w, int h);
	static void init_gpu_thread(struct exchange_buffer& buf);
	static void run(void* arg);
	void encrypt(uchar4* img, int size);
	
};


typedef Thread<exchange_buffer> GPUServer;
GPUServer* gpuThread;


	void exchange_buffer::draw(){
               double ts=gpu_timestamp();
               cpu_encrypt_string_count((uchar*)bufptr,w*h*sizeof(uchar4),iv);
               double cpu_ts=gpu_timestamp();
               gpu_updateScreen(bufptr, w,h,iv);
               X11_GL_SwapBuffers_mark();
               double gpu_ts=gpu_timestamp();
               iv=plus(iv,w*h*sizeof(uchar4)/16); // update counter
               double plus_ts=gpu_timestamp();
               frameRate+=(plus_ts-ts);
               frameRateCount++;
               if((frameRateCount%50)==0 || frameRateCount ==1 ){
                     fprintf(stderr,"total_avg_fps=%.3f, current_time_per_frame=%.3fus (gpu=%.3fus, cpu=%.3fus) \n", 1/(frameRate/((double)frameRateCount))*1e6,  plus_ts-ts,gpu_ts-cpu_ts,cpu_ts-ts);
                }
        }


	void exchange_buffer::invoke_thread(uchar* aeskey, struct bigint iv, int w, int h){
		assert(aeskey&&w&&h);
		ready_cpu=false;
		ready_gpu=false;
		frameRate=0;
		frameRateCount=0;
		inited=false;
		this->iv=iv;
		this->bufptr=(uchar4*)malloc(w*h*sizeof(uchar4));
		assert(bufptr);
		aeskeyptr=aeskey;
		this->w=w;
		this->h=h;
		
		gpuThread=new GPUServer(*this);
		if (gpuThread==NULL) assert(0);
		{
			gpuThread->start();
			Locker _l(lock);
			while(!inited) lock.conditional.wait();
		}
		fprintf(stderr,"GPU thread Initialized\n");
	}

	void exchange_buffer::init_gpu_thread(struct exchange_buffer& buf){
		buf.inited=false;
		if (init_gl()) {
		                fprintf(stderr,"Failed to init gl");
		                assert(0);
		}

	        gpu_init_CUDA();
        	printf("Allocating host and CUDA memory and loading image file...\n");
	        gpu_init_encryption(buf.aeskeyptr);

	        memset(buf.bufptr,0,buf.w*sizeof(uchar4)*buf.h);
	        assert(buf.bufptr);

	        glewInit();
        	printf("Loading extensions: %s\n", glewGetErrorString(glewInit()));
	        if (!glewIsSupported( "GL_VERSION_1_5 GL_ARB_vertex_buffer_object GL_ARB_pixel_buffer_object" )) {
                        fprintf(stderr, "Error: failed to get minimal extensions for demo\n");
                        fprintf(stderr, "This sample requires:\n");
                        fprintf(stderr, "  OpenGL version 1.5\n");
                        fprintf(stderr, "  GL_ARB_vertex_buffer_object\n");
                        fprintf(stderr, "  GL_ARB_pixel_buffer_object\n");
                        fflush(stderr);
                        exit(-1);
        	}
	        gpu_initOpenGLBuffers(buf.bufptr,buf.w,buf.h);
	        fprintf(stderr,"encryption inited,buffer created\n");
		buf.inited=true;

	}
	void exchange_buffer::run(void* arg){
		struct exchange_buffer* _buf=(struct exchange_buffer*)arg;

	Locker _l(_buf->lock);
		init_gpu_thread(*_buf);
		_buf->ready_gpu=true;
		_buf->lock.conditional.signal();

		while(1){

			while(!_buf->ready_cpu) {
				_buf->lock.conditional.wait();
			}
			_buf->draw();
			_buf->ready_gpu=true;
			_buf->ready_cpu=false;
		}
		
	}
	
	void exchange_buffer::encrypt(uchar4* img, int size){
		assert(img&&size>0);
	Locker _l(lock);
		while(!ready_gpu) lock.conditional.wait();
		ready_cpu=false;

		memcpy(bufptr,img,size);
		ready_cpu=true;
		lock.conditional.signal();
	}

exchange_buffer* server;

void gpu_updateScreen(uchar4* hSrc_encrypted, int w,int h, struct bigint iv){
	TColor *d_dst = NULL;
	size_t num_bytes;
    
	cutilSafeCall(cudaGraphicsMapResources(1, &cuda_pbo_resource, 0));
	cutilCheckMsg("cudaGraphicsMapResources failed");
	cutilSafeCall(cudaGraphicsResourceGetMappedPointer((void**)&d_dst, &num_bytes, cuda_pbo_resource));
	cutilCheckMsg("cudaGraphicsResourceGetMappedPointer failed");

	// right before we do that we decrypt
	uchar4* cudaData;
	int imgSize=sizeof(uchar4)*w*h;
	CUDA_SAFE_CALL(cudaMalloc((void**)&cudaData,imgSize));
	
	gpu_decrypt_string((uchar*)hSrc_encrypted,imgSize,(uchar**)(&cudaData),iv,true);
	CUDA_SAFE_CALL( CUDA_Memcpy2TextureArray(cudaData,w,h));
	cudaFree(cudaData);
    	cutilSafeCall( CUDA_Bind2TextureArray());

	cuda_Copy(d_dst, w, h); // create image from the bitmap

	cutilSafeCall( CUDA_UnbindTexture());
	cutilSafeCall(cudaGraphicsUnmapResources(1, &cuda_pbo_resource, 0));
    // Common display code path
	{
        glClear(GL_COLOR_BUFFER_BIT);

        glTexSubImage2D( GL_TEXTURE_2D, 0, 0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, BUFFER_DATA(0) );
        glBegin(GL_TRIANGLES);
            glTexCoord2f(0, 0); glVertex2f(-1, -1);
            glTexCoord2f(2, 0); glVertex2f(+3, -1);
            glTexCoord2f(0, 2); glVertex2f(-1, +3);
        glEnd();
        glFinish();
    	}	
}


void gpu_shutdown()
{

            printf("Shutting down...\n");
		cutilSafeCall(cudaGraphicsGLRegisterBuffer(&cuda_pbo_resource, gl_PBO, 
									   cudaGraphicsMapFlagsWriteDiscard));
                glBindBuffer(GL_PIXEL_UNPACK_BUFFER_ARB, 0);
                glDeleteBuffers(1, &gl_PBO);
                glDeleteTextures(1, &gl_Tex);
                cutilSafeCall( CUDA_FreeArray() );
    		glDeleteProgramsARB(1, &shader);
		
		
            printf("Shutdown done. -> TODO: bzero all the GPU memory!!!\n");
            cutilDeviceReset();
}

static GLuint compileASMShader(GLenum program_type, const char *code)
{
    GLuint program_id;
    glGenProgramsARB(1, &program_id);
    glBindProgramARB(program_type, program_id);
    glProgramStringARB(program_type, GL_PROGRAM_FORMAT_ASCII_ARB, (GLsizei) strlen(code), (GLubyte *) code);

    GLint error_pos;
    glGetIntegerv(GL_PROGRAM_ERROR_POSITION_ARB, &error_pos);
    if (error_pos != -1) {
        const GLubyte *error_string;
        error_string = glGetString(GL_PROGRAM_ERROR_STRING_ARB);
        fprintf(stderr, "Program error at position: %d\n%s\n", (int)error_pos, error_string);
        return 0;
    }
    return program_id;
}

void gpu_initOpenGLBuffers(uchar4 *h_Src,int imageW,int imageH  )
{
	assert(h_Src&&imageW&&imageH);
        cutilSafeCall( CUDA_MallocArray(&h_Src, imageW, imageH) );
	printf("Creating GL texture %dx%d...\n",imageW,imageH);
        glEnable(GL_TEXTURE_2D);
        glGenTextures(1, &gl_Tex);
        glBindTexture(GL_TEXTURE_2D, gl_Tex);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, imageW, imageH, 0, GL_RGBA, GL_UNSIGNED_BYTE, h_Src);
   	
	printf("Texture created.\n");

	printf("Creating PBO...\n");

        glGenBuffers(1, &gl_PBO);
        glBindBuffer(GL_PIXEL_UNPACK_BUFFER_ARB, gl_PBO);
        glBufferData(GL_PIXEL_UNPACK_BUFFER_ARB, imageW * imageH * 4, h_Src, GL_STREAM_COPY);
        //While a PBO is registered to CUDA, it can't be used 
        //as the destination for OpenGL drawing calls.
        //But in our particular case OpenGL is only used 
        //to display the content of the PBO, specified by CUDA kernels,
        //so we need to register/unregister it only once.
	// DEPRECATED: cutilSafeCall( cudaGLRegisterBufferObject(gl_PBO) );
	cutilSafeCall(cudaGraphicsGLRegisterBuffer(&cuda_pbo_resource, gl_PBO, 
					       cudaGraphicsMapFlagsWriteDiscard));
        //CUT_CHECK_ERROR_GL();
        printf("PBO created.\n");

    	// load shader program
	shader = compileASMShader(GL_FRAGMENT_PROGRAM_ARB, shader_code);
 
}

void gpu_init_CUDA(){
	cudaGLSetGLDevice( cutGetMaxGflopsDeviceId() );
}
void gpu_init_encryption(uchar* aeskey)
{
//////////////// init GPU encryption ////
    gpu_init(NULL, NULL);
    gpu_set_key((uchar*)aeskey);
    cpu_init();
    cpu_set_key((uchar*)aeskey);
}

void gpu_invoke_server(uchar* aeskey, struct bigint iv, int w, int h){
	server=new exchange_buffer();

	server->invoke_thread(aeskey,iv,  w, h);
}

void gpu_send(uchar4* img, int size){
	server->encrypt(img,size);
}


	
