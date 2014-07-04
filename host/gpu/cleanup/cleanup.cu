#include <cuda.h>
#include <stdio.h>

int main(){
	
	void** device_mem[2000];
	int alloced[2000];
	void* device=NULL;
	long toAlloc;
	int i;
	long totalAlloc=0;
	for (i=0;i<1600;i++){
		float alloc=1.5;
		bool success=false;
		while(alloc>0){
			toAlloc=alloc*1024*1024;
			if (cudaMalloc((void**)&device_mem[i],toAlloc)!=cudaSuccess){
			alloc-=0.1;
			fprintf(stderr,"%ld %s\n",toAlloc,cudaGetErrorString(cudaPeekAtLastError()));
			}else{
				success=true;
				totalAlloc+=toAlloc;
				alloced[i]=toAlloc;
				break;
				
			}
		}
		if(success==false) break;
	}
	
	fprintf(stderr,"Allocated %ld GB of memory\n",totalAlloc );
	fprintf(stderr,"Cleaning\n");
	for(int z=0;z<=i;z++){
		fprintf(stderr,"%d %p\n",z,device_mem[z]);
		if (cudaMemset(device_mem[z],0,alloced[z])!=cudaSuccess){
			printf("Memory cannot be accessed -> %z??\n");
		}
	}
//	cudaFree(device);
	return 0;
}

