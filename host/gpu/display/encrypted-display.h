#ifndef ENCRYPTED_DISPLAY_H
#define ENCRYPTED_DISPLAY_H
#include "aes_gpu.h"

// init all the GL buffers. h_Src should be allocated prior to the call
void gpu_initOpenGLBuffers(uchar4 *h_Src,int imageW,int imageH  );
// main function to update the screen for every change
void gpu_updateScreen(uchar4* hSrc, int w,int h, struct bigint iv);
// to be called for clean up
void gpu_shutdown(void);
// init the encrypting backed - both CPU and GPU - to be called after GPU initialized
void gpu_init_encryption(uchar* aeskey);
// init CUDA context
void gpu_init_CUDA(void);

void gpu_invoke_server(uchar* aeskey, struct bigint iv, int w, int h);

void gpu_send(uchar4* img, int size);
#endif
