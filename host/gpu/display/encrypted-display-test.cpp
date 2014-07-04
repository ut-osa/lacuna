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



// OpenGL Graphics includes
#include <GL/glew.h>
#include <GL/freeglut.h>

// CUDA utilities and system includes
#include <cutil_inline.h>    // includes cuda.h and cuda_runtime_api.h
#include <cutil_gl_inline.h> // includes cuda_gl_interop.h
//#include <rendercheck_gl.h>

// Includes
#include <stdio.h>
#include <stdlib.h>
#include <string.h>




////////////////////////////////////////////////////////////////////////////////
// Global data handlers and parameters
////////////////////////////////////////////////////////////////////////////////
//OpenGL PBO and texture "names"

////////////////////////////////////////////////////////////////////////////////
// Main program
////////////////////////////////////////////////////////////////////////////////
int  g_Kernel = 0;
bool g_FPS = false;
unsigned int hTimer;


const int frameN = 24;
int frameCounter = 0;

typedef unsigned char uchar;
extern "C"{
#include "aes_gpu.h"
#include "bmploader.h"
#include "encrypted-display.h"
}
int imageW,imageH;
struct bigint iv;
uchar4* h_Src;


int fpsCount = 0;        // FPS count for averaging
int fpsLimit = 1;        // FPS limit for sampling
unsigned int frameCount = 0;

// CheckFBO/BackBuffer class objects
//CFrameBufferObject  *g_FrameBufferObject = NULL;

#define REFRESH_DELAY	  10 //ms

void computeFPS()
{
	char fps[256];
    frameCount++;
    fpsCount++;
    if (fpsCount == fpsLimit) {
   	float ifps = 1.f / (cutGetAverageTimerValue(hTimer) / 1000.f);
        sprintf(fps, " %3.1f fps", ifps);  
	g_FPS=true;
        fpsCount = 0; 
        cutilCheckError(cutResetTimer(hTimer));  
    }
}

void updateScreen_local(){
	cutStartTimer(hTimer);

	if(frameCounter++ == 0) cutResetTimer(hTimer);
 	gpu_updateScreen(h_Src, imageW,imageH,iv);   
    
	if(frameCounter == frameN){
        frameCounter = 0;
        if(g_FPS){
            printf("FPS: %3.1f\n", frameN / (cutGetTimerValue(hTimer) * 0.001) );
            g_FPS = false;
        }
    }
}

void displayFunc(void){
	updateScreen_local();

	glutSwapBuffers();

	cutStopTimer(hTimer);
	computeFPS();
}

void timerEvent(int value)
{
   	glutPostRedisplay();
	glutTimerFunc(REFRESH_DELAY, timerEvent, 0);
}


void shutdown_local(uchar4* h_Src)
{

            printf("Shutting down...\n");
                cutilCheckError( cutStopTimer(hTimer)   );
                cutilCheckError( cutDeleteTimer(hTimer) );
                free(h_Src);
	   gpu_shutdown();		
            printf("Shutdown done.\n");
}


int initGL( int *argc, char **argv )
{
    printf("Initializing GLUT...\n");
    glutInit(argc, argv);
    glutInitDisplayMode(GLUT_RGBA | GLUT_DOUBLE);
    glutInitWindowSize(imageW, imageH);
    glutInitWindowPosition(512 - imageW / 2, 384 - imageH / 2);
    glutCreateWindow(argv[0]);

    glewInit();
    printf("Loading extensions: %s\n", glewGetErrorString(glewInit()));
	if (!glewIsSupported( "GL_VERSION_1_5 GL_ARB_vertex_buffer_object GL_ARB_pixel_buffer_object" )) {
			fprintf(stderr, "Error: failed to get minimal extensions for demo\n");
			fprintf(stderr, "This sample requires:\n");
			fprintf(stderr, "  OpenGL version 1.5\n");
			fprintf(stderr, "  GL_ARB_vertex_buffer_object\n");
			fprintf(stderr, "  GL_ARB_pixel_buffer_object\n");
  			fflush(stderr);
		        return CUTFalse;
	}
    return 0;
}

int main(int argc, char **argv)
{
	
        

// First initialize OpenGL context, so we can properly set the GL for CUDA.
// This is necessary in order to achieve optimal performance with OpenGL/CUDA interop.
	const char* enc_filename="data/portrait_noise_resized.bmp";
	uchar4* bmpdata;
	LoadBMPFile(&bmpdata, &imageW, &imageH,enc_filename);
	
	//initGL( &argc, argv );
	
	//gpu_init_CUDA();
	printf("Allocating host and CUDA memory and loading image file...\n");
	char* aeskey="111 111 111 111 ";
	//gpu_init_encryption((uchar*)aeskey);	

   // load image
	
	
	int imgSize=imageW*imageH*sizeof(uchar4);
	iv.d[0]=1;
	iv.d[1]=2;
	iv.d[2]=3;
	iv.d[3]=4;
	
	cpu_encrypt_string_count( (uchar*)bmpdata,imgSize,iv);
	h_Src=(uchar4*)bmpdata;
	fprintf(stderr,"encryption inited,buffer created\n");

        gpu_initOpenGLBuffers(h_Src,imageW,imageH);

	glutDisplayFunc(displayFunc);
	
	cutilCheckError( cutCreateTimer(&hTimer) );
	cutilCheckError( cutStartTimer(hTimer)   );
	glutTimerFunc(REFRESH_DELAY, timerEvent,0);
	glutMainLoop();
	shutdown_local(h_Src);
	cutilDeviceReset();
}
