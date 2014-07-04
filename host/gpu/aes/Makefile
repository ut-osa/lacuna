include ../../../cuda_env.mak

LIBS=
CUDA_LIBS=-L$(CUDA_ROOT)/lib64 -lcudart -L$(CUDA_SDK_ROOT)/C/lib/ -lcutil_x86_64  -L../../../crypto/libprivcrypt/ -lprivcrypt
CUDA_INCLUDE=-I$(CUDA_ROOT)/include -I$(CUDA_SDK_ROOT)/C/common/inc
#NVCC=nvcc --maxrregcount 24 --gpu-architecture=sm_21 $(CUDA_INCLUDE)
NVCC=nvcc --gpu-architecture=sm_21 $(CUDA_INCLUDE) -Xptxas -v  --debug
CC=g++ $(CUDA_INCLUDE) -Wall -Wno-write-strings -g  -O0

.PHONY: all debug clean

TARGETS := enc libaes.a
all: $(TARGETS)

libaes.a: libaes.a(aes.o aes_cpu.o)

enc: aes.o aes_cpu.o enc_dec.o bmploader.o 
	$(CC) aes.o aes_cpu.o enc_dec.o bmploader.o  -o enc   $(CUDA_LIBS)

aes.o: aes_gpu.h aes.cu
	$(NVCC) aes.cu -c 

aes_cpu.o:  aes_cpu.cpp 
	$(CC) -c aes_cpu.cpp

enc_dec.o: enc_dec.cpp
	$(CC) -c enc_dec.cpp

bmploader.o: bmploader.cpp
	$(CC) -c bmploader.cpp

clean:
	rm -f $(TARGETS) *.o
