include ../../../cuda_env.mak

LIBS=-L../aes -laes -lGL -lGLU -lX11 -lXi -lXmu  -lglut -L$(CUDA_SDK_ROOT)/C/common/lib/linux -lGLEW_x86_64 -lgcrypt
CUDA_LIBS=-L$(CUDA_ROOT)/lib64 -lcudart -L$(CUDA_SDK_ROOT)/C/lib/ -lcutil_x86_64  -L/usr/lib 
CUDA_INCLUDE=-I$(CUDA_ROOT)/include -I$(CUDA_SDK_ROOT)/C/common/inc -I../aes/ -I../../SDL-1.2.15/include
#NVCC=nvcc --maxrregcount 24 --gpu-architecture=sm_21 $(CUDA_INCLUDE)
NVCC=nvcc --gpu-architecture=sm_21 $(CUDA_INCLUDE) -Xptxas -v  --debug
CC=g++ $(CUDA_INCLUDE) -Wall -Wno-write-strings -g -O2

ALL: libencrypted-display.a

encrypted-display-test: encrypted-display.o bmploader.o encrypted-display-backend.o encrypted-display-test.o
	$(CC)  encrypted-display.o bmploader.o encrypted-display-backend.o encrypted-display-test.o -o encrypted-display-test  $(LIBS) $(CUDA_LIBS)

libencrypted-display.a: encrypted-display.o encrypted-display-backend.o
	ar -r libencrypted-display.a encrypted-display.o encrypted-display-backend.o

encrypted-display-test.o:  encrypted-display.h   encrypted-display-test.cpp
	$(CC) -c encrypted-display-test.cpp 

encrypted-display.o:  encrypted-display-backend.h   encrypted-display.cpp encrypted-display.h
	$(CC) -c encrypted-display.cpp 

encrypted-display-backend.o:  encrypted-display.h encrypted-display-backend.cu
	$(NVCC) -c encrypted-display-backend.cu

bmploader.o: bmploader.cpp 
	$(CC) -c bmploader.cpp


PHONY: debug clean

clean:
	rm -f enc-display *.o

