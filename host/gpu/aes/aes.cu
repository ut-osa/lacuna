
#include <stdarg.h>
#include <cutil.h>
#include <stdio.h>
#include <stdlib.h>
extern "C"{
#include "aes_gpu.h"
#include "timer.h"
}
#include <stdlib.h>

#define _THREADS_PER_BLOCK 256
const int MaxThreadsPerBlock = _THREADS_PER_BLOCK;




/* Global array : step for optimization*/
uchar* d_expkey  = NULL; 
uchar* d_sbox  = NULL; 
uchar* d_invsbox  = NULL; 
uchar* d_xtimee  = NULL; 
uchar* d_xtimeb  = NULL; 
uchar* d_xtimed  = NULL; 
uchar* d_xtime9  = NULL; 
uchar* d_in_enc  = NULL; 
uchar* d_out_enc = NULL; 
uchar* d_in_dec  = NULL; 
uchar* d_out_dec = NULL; 

uchar* h_in_enc, *h_out_enc;
uchar* h_in_dec, *h_out_dec;



int gpu_fd=-1;
texture<unsigned char, 1, cudaReadModeElementType> tex_sbox;
texture<unsigned char, 1, cudaReadModeElementType> tex_invsbox;
#define SBOX(i)         ((unsigned char)tex1Dfetch(tex_sbox,(i)))
#define INVSBOX(i)         ((unsigned char)tex1Dfetch(tex_invsbox,(i)))

texture<unsigned char, 1, cudaReadModeElementType> tex_xtimee;
texture<unsigned char, 1, cudaReadModeElementType> tex_xtimeb;
texture<unsigned char, 1, cudaReadModeElementType> tex_xtimed;
texture<unsigned char, 1, cudaReadModeElementType> tex_xtime9;
#define XTIMEE(i)         ((unsigned char)tex1Dfetch(tex_xtimee,(i)))
#define XTIMEB(i)         ((unsigned char)tex1Dfetch(tex_xtimeb,(i)))
#define XTIMED(i)         ((unsigned char)tex1Dfetch(tex_xtimed,(i)))
#define XTIME9(i)         ((unsigned char)tex1Dfetch(tex_xtime9,(i)))

/* The global data */
__device__ __constant__ uchar _Sbox[256];
__device__ __constant__ uchar _InvSbox[256];
__device__ __constant__ uchar _Xtime2Sbox[256];
__device__ __constant__ uchar _Xtime3Sbox[256];
__device__ __constant__ uchar _Xtime2[256];
__device__ __constant__ uchar _Xtime9[256];
__device__ __constant__ uchar _XtimeB[256];
__device__ __constant__ uchar _XtimeD[256];
__device__ __constant__ uchar _XtimeE[256];

/* GPU Functions */
void _ShiftRows (uchar *state);
void _InvShiftRows (uchar *state);
void _MixSubColumns (uchar *state);
void _InvMixSubColumns (uchar *state);
void _AddRoundKey (unsigned *state, unsigned *key);
void _Encrypt (uchar *in, uchar *expkey, uchar *out);
void _Decrypt (uchar *in, uchar *expkey, uchar *out);
	



//prefetch 
union Int4Char{
	uint i32;
	uchar i8[4];
};
//#define SBOX(x) _Sbox[(x)];
#define FETCH_COL(out,input,col) (out)=((uint*)input)[(col)];
#define SAVE_COL(out,input,col) ((uint*)out)[(col)]=(input);

__device__ struct bigint gpu_bigint_plus(const struct bigint& x, uint y){
        char i=0;                                                              
        char carry=0;                                                          
	struct bigint res;
        res.d[0]=x.d[0];
        res.d[1]=x.d[1];                                                       
        res.d[2]=x.d[2];
        res.d[3]=x.d[3];
	uchar* v=(uchar*)res.d;                                 
	for(int j=y;j>0;j-=MAXVAL){                      
		uchar tmp_y=j>MAXVAL?MAXVAL:j;
	        for(i=15;i>=0;i--){
        	       if (MAXVAL != v[i] && MAXVAL- carry - v[i] >= tmp_y) {         
	                        v[i]=v[i]+tmp_y+carry;
	                        carry=0;                                               
	                        break;
        	        }                                                              
	                v[i]=tmp_y-(MAXVAL+1-v[i]-carry);
	                tmp_y=0;
	                carry=1;                                                       
        	}
	}
	return res;

}

__device__ void _ShiftRows_opt (uchar* input){
	Int4Char col0,col1,col2,col3;
	Int4Char tmp;
	FETCH_COL(col0.i32, input,0);
	FETCH_COL(col1.i32, input,1);
	FETCH_COL(col2.i32, input,2);
	FETCH_COL(col3.i32, input,3);
	
// r0	
	col0.i8[0]=SBOX(col0.i8[0]);
	col1.i8[0]=SBOX(col1.i8[0]);
	col2.i8[0]=SBOX(col2.i8[0]);
	col3.i8[0]=SBOX(col3.i8[0]);
	
// r1	

	tmp.i8[1]=SBOX(col0.i8[1]);
	col0.i8[1]=SBOX(col1.i8[1]);
	col1.i8[1]=SBOX(col2.i8[1]);
	col2.i8[1]=SBOX(col3.i8[1]);
	col3.i8[1]=tmp.i8[1];

// r2	
	tmp.i8[2]=SBOX(col0.i8[2]);
	tmp.i8[3]=SBOX(col1.i8[2]);
	col0.i8[2]=SBOX(col2.i8[2]);
	col1.i8[2]=SBOX(col3.i8[2]);
	col2.i8[2]=tmp.i8[2];
	col3.i8[2]=tmp.i8[3];
// r3
	tmp.i8[3]=SBOX(col3.i8[3]);
	col3.i8[3]=SBOX(col2.i8[3]);
	col2.i8[3]=SBOX(col1.i8[3]);
	col1.i8[3]=SBOX(col0.i8[3]);
	col0.i8[3]=tmp.i8[3];

	SAVE_COL(input,col0.i32,0);
	SAVE_COL(input,col1.i32,1);
	SAVE_COL(input,col2.i32,2);
	SAVE_COL(input,col3.i32,3);
}

/*
// exchanges columns in each of 4 rows
// row0 - unchanged, row1- shifted left 1, 
// row2 - shifted left 2 and row3 - shifted left 3
__device__ void _ShiftRows (uchar *state)
{
	//printf("  - ShiftRows\n");
	uchar tmp;
	

	// just substitute row 0
	state[0] = _Sbox[state[0]], state[4]  = _Sbox[state[4]];
	state[8] = _Sbox[state[8]], state[12] = _Sbox[state[12]];

	// rotate row 1
	tmp = _Sbox[state[1]], state[1] = _Sbox[state[5]];
	state[5] = _Sbox[state[9]], state[9] = _Sbox[state[13]], state[13] = tmp;

	// rotate row 2
	tmp = _Sbox[state[2]], state[2] = _Sbox[state[10]], state[10] = tmp;
	tmp = _Sbox[state[6]], state[6] = _Sbox[state[14]], state[14] = tmp;

	// rotate row 3
	tmp = _Sbox[state[15]], state[15] = _Sbox[state[11]];
	state[11] = _Sbox[state[7]], state[7] = _Sbox[state[3]], state[3] = tmp;
}

*/

__device__ void _InvShiftRows_opt (uchar* input){
	Int4Char col0,col1,col2,col3;
	Int4Char tmp;
	FETCH_COL(col0.i32, input,0);
	FETCH_COL(col1.i32, input,1);
	FETCH_COL(col2.i32, input,2);
	FETCH_COL(col3.i32, input,3);
	
// r0	
	col0.i8[0]=INVSBOX(col0.i8[0]);
	col1.i8[0]=INVSBOX(col1.i8[0]);
	col2.i8[0]=INVSBOX(col2.i8[0]);
	col3.i8[0]=INVSBOX(col3.i8[0]);
	
// r1	

	tmp.i8[1]=INVSBOX(col3.i8[1]);
	col3.i8[1]=INVSBOX(col2.i8[1]);
	col2.i8[1]=INVSBOX(col1.i8[1]);
	col1.i8[1]=INVSBOX(col0.i8[1]);
	col3.i8[1]=tmp.i8[1];

// r2	
	tmp.i8[2]=INVSBOX(col0.i8[2]);
	tmp.i8[3]=INVSBOX(col1.i8[2]);
	col0.i8[2]=INVSBOX(col2.i8[2]);
	col1.i8[2]=INVSBOX(col3.i8[2]);
	col2.i8[2]=tmp.i8[2];
	col3.i8[2]=tmp.i8[3];
// r3
	tmp.i8[3]=INVSBOX(col0.i8[3]);
	col0.i8[3]=INVSBOX(col1.i8[3]);
	col1.i8[3]=INVSBOX(col2.i8[3]);
	col2.i8[3]=INVSBOX(col0.i8[3]);
	col3.i8[3]=tmp.i8[3];

	SAVE_COL(input,col0.i32,0);
	SAVE_COL(input,col1.i32,1);
	SAVE_COL(input,col2.i32,2);
	SAVE_COL(input,col3.i32,3);
}
// restores columns in each of 4 rows
// row0 - unchanged, row1- shifted right 1, 
// row2 - shifted right 2 and row3 - shifted right 3
__device__ void _InvShiftRows (uchar *state)
{
	uchar tmp,tmp2;

	// restore row 0
	state[0] = _InvSbox[state[0]], state[4] = _InvSbox[state[4]];
	state[8] = _InvSbox[state[8]], state[12] = _InvSbox[state[12]];

	// restore row 1
	tmp = _InvSbox[state[13]], state[13] = _InvSbox[state[9]];
	state[9] = _InvSbox[state[5]], state[5] = _InvSbox[state[1]], state[1] = tmp;

	// restore row 2
	tmp = _InvSbox[state[2]]; 
	tmp2 = _InvSbox[state[6]]; 
	
	state[2] = _InvSbox[state[10]];
	state[6] = _InvSbox[state[14]]; 
       	state[10] = tmp;
	state[14] = tmp2;

	// restore row 3
	tmp = _InvSbox[state[3]], state[3] = _InvSbox[state[7]];
	state[7] = _InvSbox[state[11]], state[11] = _InvSbox[state[15]], state[15] = tmp;
}


#define xtime32(x) ((((x) >> 7) & 0x01010101) * 0x1b) ^ (((x) << 1) & 0xfefefefe)

__device__ void _InvMixSubColumns_opt(uchar* state) {
	Int4Char col0,col1,col2,col3;
	Int4Char tmp;
	FETCH_COL(col0.i32, state,0);
	FETCH_COL(col1.i32, state,1);
	FETCH_COL(col2.i32, state,2);
	FETCH_COL(col3.i32, state,3);
      
	tmp.i8[0]= INVSBOX(XTIMEE(col0.i8[0])^XTIMEB(col0.i8[1])^XTIMED(col0.i8[2])^XTIME9(col0.i8[3]));
	tmp.i8[1]= INVSBOX(XTIME9(col3.i8[0])^XTIMEE(col3.i8[1])^XTIMEB(col3.i8[2])^XTIMED(col3.i8[3]));
	tmp.i8[2]= INVSBOX(XTIMED(col2.i8[0])^XTIME9(col2.i8[1])^XTIMEE(col2.i8[2])^XTIMEB(col2.i8[3]));
	tmp.i8[3]= INVSBOX(XTIMEB(col1.i8[0])^XTIMED(col1.i8[1])^XTIME9(col1.i8[2])^XTIMEE(col1.i8[3]));
	SAVE_COL(state,tmp.i32,0);
	

	tmp.i8[0]= INVSBOX(XTIMEE(col1.i8[0])^XTIMEB(col1.i8[1])^XTIMED(col1.i8[2])^XTIME9(col1.i8[3]));
	tmp.i8[1]= INVSBOX(XTIME9(col0.i8[0])^XTIMEE(col0.i8[1])^XTIMEB(col0.i8[2])^XTIMED(col0.i8[3]));
	tmp.i8[2]= INVSBOX(XTIMED(col3.i8[0])^XTIME9(col3.i8[1])^XTIMEE(col3.i8[2])^XTIMEB(col3.i8[3]));
	tmp.i8[3]= INVSBOX(XTIMEB(col2.i8[0])^XTIMED(col2.i8[1])^XTIME9(col2.i8[2])^XTIMEE(col2.i8[3]));
	SAVE_COL(state,tmp.i32,1);

	tmp.i8[0]= INVSBOX(XTIMEE(col2.i8[0])^XTIMEB(col2.i8[1])^XTIMED(col2.i8[2])^XTIME9(col2.i8[3]));
	tmp.i8[1]= INVSBOX(XTIME9(col1.i8[0])^XTIMEE(col1.i8[1])^XTIMEB(col1.i8[2])^XTIMED(col1.i8[3]));
	tmp.i8[2]= INVSBOX(XTIMED(col0.i8[0])^XTIME9(col0.i8[1])^XTIMEE(col0.i8[2])^XTIMEB(col0.i8[3]));
	tmp.i8[3]= INVSBOX(XTIMEB(col3.i8[0])^XTIMED(col3.i8[1])^XTIME9(col3.i8[2])^XTIMEE(col3.i8[3]));
	SAVE_COL(state,tmp.i32,2);

	tmp.i8[0]= INVSBOX(XTIMEE(col3.i8[0])^XTIMEB(col3.i8[1])^XTIMED(col3.i8[2])^XTIME9(col3.i8[3]));
	tmp.i8[1]= INVSBOX(XTIME9(col2.i8[0])^XTIMEE(col2.i8[1])^XTIMEB(col2.i8[2])^XTIMED(col2.i8[3]));
	tmp.i8[2]= INVSBOX(XTIMED(col1.i8[0])^XTIME9(col1.i8[1])^XTIMEE(col1.i8[2])^XTIMEB(col1.i8[3]));
	tmp.i8[3]= INVSBOX(XTIMEB(col0.i8[0])^XTIMED(col0.i8[1])^XTIME9(col0.i8[2])^XTIMEE(col0.i8[3]));
	SAVE_COL(state,tmp.i32,3);
}

__device__ void MixColumn(Int4Char& ib)
{
        Int4Char ibTmp;
    	ibTmp.i32 = (ib.i32 >> 8) | (ib.i32 << 24);
        ibTmp.i32 = ibTmp.i32 ^ ib.i32;
        ib.i32 = ib.i32 ^ xtime32(ibTmp.i32);
        ibTmp.i32 = ibTmp.i32 ^ ((ibTmp.i32 << 16) | (ibTmp.i32 >> 16));
        ib.i32 = ib.i32 ^ ibTmp.i32;
}


__device__ void _MixColumns_opt(uchar* state){
	Int4Char col0,col1,col2,col3;

	FETCH_COL(col0.i32,state,0);
	FETCH_COL(col1.i32,state,1);
	FETCH_COL(col2.i32,state,2);
	FETCH_COL(col3.i32,state,3);

	MixColumn(col0);
	MixColumn(col1);
	MixColumn(col2);
	MixColumn(col3);
	
	SAVE_COL(state,col0.i32,0);
	SAVE_COL(state,col1.i32,1);
	SAVE_COL(state,col2.i32,2);
	SAVE_COL(state,col3.i32,3);
}


__device__ void _InvMixSubColumns (uchar *state)
{
	uchar tmp[4 * Nb];
	int i;

	// restore column 0
	tmp[0] = _XtimeE[state[0]] ^ _XtimeB[state[1]] ^ _XtimeD[state[2]] ^ _Xtime9[state[3]];
	tmp[5] = _Xtime9[state[0]] ^ _XtimeE[state[1]] ^ _XtimeB[state[2]] ^ _XtimeD[state[3]];
	tmp[10] = _XtimeD[state[0]] ^ _Xtime9[state[1]] ^ _XtimeE[state[2]] ^ _XtimeB[state[3]];
	tmp[15] = _XtimeB[state[0]] ^ _XtimeD[state[1]] ^ _Xtime9[state[2]] ^ _XtimeE[state[3]];

	// restore column 1
	tmp[4] = _XtimeE[state[4]] ^ _XtimeB[state[5]] ^ _XtimeD[state[6]] ^ _Xtime9[state[7]];
	tmp[9] = _Xtime9[state[4]] ^ _XtimeE[state[5]] ^ _XtimeB[state[6]] ^ _XtimeD[state[7]];
	tmp[14] = _XtimeD[state[4]] ^ _Xtime9[state[5]] ^ _XtimeE[state[6]] ^ _XtimeB[state[7]];
	tmp[3] = _XtimeB[state[4]] ^ _XtimeD[state[5]] ^ _Xtime9[state[6]] ^ _XtimeE[state[7]];

	// restore column 2
	tmp[8] = _XtimeE[state[8]] ^ _XtimeB[state[9]] ^ _XtimeD[state[10]] ^ _Xtime9[state[11]];
	tmp[13] = _Xtime9[state[8]] ^ _XtimeE[state[9]] ^ _XtimeB[state[10]] ^ _XtimeD[state[11]];
	tmp[2]  = _XtimeD[state[8]] ^ _Xtime9[state[9]] ^ _XtimeE[state[10]] ^ _XtimeB[state[11]];
	tmp[7]  = _XtimeB[state[8]] ^ _XtimeD[state[9]] ^ _Xtime9[state[10]] ^ _XtimeE[state[11]];

	// restore column 3
	tmp[12] = _XtimeE[state[12]] ^ _XtimeB[state[13]] ^ _XtimeD[state[14]] ^ _Xtime9[state[15]];
	tmp[1] = _Xtime9[state[12]] ^ _XtimeE[state[13]] ^ _XtimeB[state[14]] ^ _XtimeD[state[15]];
	tmp[6] = _XtimeD[state[12]] ^ _Xtime9[state[13]] ^ _XtimeE[state[14]] ^ _XtimeB[state[15]];
	tmp[11] = _XtimeB[state[12]] ^ _XtimeD[state[13]] ^ _Xtime9[state[14]] ^ _XtimeE[state[15]];
	for( i=0; i <  4*Nb; i++){
		state[i] = _InvSbox[ tmp[i] ];
	}
}

// encrypt/decrypt columns of the key
// n.b. you can replace this with
//      byte-wise xor if you wish.
__device__ void _AddRoundKey (unsigned *state, unsigned *key)
{
	for ( int i = 0; i < 4; ++i )
		state[i] ^= key[i];
}

__device__ void _DecryptOneAESBlock(uchar *in, uchar* expkey){

	_AddRoundKey( (unsigned*)(in), (unsigned*)expkey + Nr * Nb );
	_InvShiftRows( in );
	int round=Nr;
	for ( round = Nr-1; round>0; round-- )
	{
		_AddRoundKey( (unsigned*)(in), (unsigned*)expkey + round * Nb );
		_InvMixSubColumns_opt( in );
	}
	_AddRoundKey( (unsigned*)(in), (unsigned*)expkey + round * Nb );

}

__device__ void _EncryptOneAESBlock(uchar *in, uchar* expkey){
	_AddRoundKey( (unsigned*)(in), (unsigned*)expkey );
	
	int round=1;

	for( round = 1; round < Nr; round++)
	{
		_ShiftRows_opt( in );
		_MixColumns_opt(in);
		_AddRoundKey( (unsigned*)(in), (unsigned*)expkey + round*Nb );
	}

	_ShiftRows_opt( in );
	_AddRoundKey( (unsigned*)(in), (unsigned*)expkey + round*Nb );
	
}


__device__ void _ParallelXor_opt(struct bigint& buf, const struct bigint* iv){
	buf.d[0]^=iv->d[0];
	buf.d[1]^=iv->d[1];
	buf.d[2]^=iv->d[2];
	buf.d[3]^=iv->d[3];
}



 __global__ void encrypt_decrypt_counter(uchar *in, uchar* expkey, uchar *out, struct bigint iv ) 
{ 

	__shared__ uchar bufkey[176];

	uint block_start=blockIdx.x*blockDim.x;
	uint myId=threadIdx.x+block_start;
	struct bigint myiv=gpu_bigint_plus(iv,myId);

	if (threadIdx.x<44){
		((int*)bufkey)[threadIdx.x]=((int*)expkey)[threadIdx.x];
	}
	__syncthreads();
	_EncryptOneAESBlock((uchar*)myiv.d,(uchar*)bufkey);
	_ParallelXor_opt(myiv,((struct bigint*)in+myId));
	((struct bigint*)out)[myId]=myiv;
}



#define MAX_BLOCK_SIZE (1024*1024*16)


 __host__ int gpu_process_string(uchar* in, int length,  uchar** d_out, struct bigint iv, bool keep_result_in_gpu=0)
{

	if (length > MAX_BLOCK_SIZE) {
		fprintf(stderr,"Data size exceeds %d\n", MAX_BLOCK_SIZE);
		return -1;
	}

	if (length%(_THREADS_PER_BLOCK*16)) {
		fprintf(stderr,"GPU can be used only to encrypt multiple of  %d bytes\n", _THREADS_PER_BLOCK*16);
		return -1;
	}


	memcpy(h_in_enc,in, length);


	uchar* h_data=h_in_enc;
	uchar* d_data=d_in_enc;
	uchar* d_out_data=d_out_enc;

	CUDA_SAFE_CALL( cudaMemcpy( (void*)d_data, (void*)h_data, length, cudaMemcpyHostToDevice ) );

	uint units = length / 16;
	uint threads = (units >= MaxThreadsPerBlock) ? MaxThreadsPerBlock : (units % MaxThreadsPerBlock);
	uint blocks = units / threads;

	if(blocks == 0)	blocks = 1;
	char* no_encrypt=getenv(NO_ENCRYPT_ENV);
	if (!no_encrypt)
	{
		encrypt_decrypt_counter<<<blocks,threads,0>>>(d_data, d_expkey, d_out_data,  iv);
		cudaThreadSynchronize();
	}
	if (!keep_result_in_gpu){
		CUDA_SAFE_CALL( cudaMemcpy( (void*)h_data, (void*)d_out_data, length, cudaMemcpyDeviceToHost) );
		cudaThreadSynchronize();
		memcpy( *d_out,  h_data, length);
	}else{
		*d_out=d_out_data;
	}
	CUDA_SAFE_CALL(cudaPeekAtLastError());

	return 0;
}

 int gpu_encrypt_string(uchar* in, int length,  uchar** d_out, struct bigint iv, bool keep_in_gpu_mem=0){
	return	gpu_process_string(in,length,d_out,iv,keep_in_gpu_mem);
}


 __host__ int gpu_decrypt_string(uchar* in, int length,  uchar** d_out, struct bigint iv, bool keep_in_gpu_mem=0){
	return	gpu_process_string(in,length,d_out,iv,keep_in_gpu_mem);
}

 __host__ void gpu_init(int argv, char** argc)
{
	
	//CUDA_SAFE_CALL( cudaSetDeviceFlags 	( cudaDeviceScheduleSpin	 ) );
	//CUT_DEVICE_INIT(argc, argv);

	CUDA_SAFE_CALL( cudaMalloc( (void**) &d_sbox, 256));
	CUDA_SAFE_CALL( cudaMemcpy( d_sbox, Sbox, 256, cudaMemcpyHostToDevice) );
	CUDA_SAFE_CALL(cudaBindTexture(0, tex_sbox, d_sbox ));

	CUDA_SAFE_CALL( cudaMalloc( (void**) &d_invsbox, 256));
	CUDA_SAFE_CALL( cudaMemcpy( d_invsbox, InvSbox, 256, cudaMemcpyHostToDevice) );
	CUDA_SAFE_CALL(cudaBindTexture(0, tex_invsbox, d_invsbox ));

	CUDA_SAFE_CALL( cudaMalloc( (void**) &d_xtimee, 256));
	CUDA_SAFE_CALL( cudaMalloc( (void**) &d_xtimeb, 256));
	CUDA_SAFE_CALL( cudaMalloc( (void**) &d_xtimed, 256));
	CUDA_SAFE_CALL( cudaMalloc( (void**) &d_xtime9, 256));
	
	CUDA_SAFE_CALL( cudaMemcpy(d_xtimee , XtimeE, 256, cudaMemcpyHostToDevice) );
	CUDA_SAFE_CALL( cudaMemcpy(d_xtimeb , XtimeB, 256, cudaMemcpyHostToDevice) );
	CUDA_SAFE_CALL( cudaMemcpy(d_xtimed , XtimeD, 256, cudaMemcpyHostToDevice) );
	CUDA_SAFE_CALL( cudaMemcpy(d_xtime9 , Xtime9, 256, cudaMemcpyHostToDevice) );
	
	CUDA_SAFE_CALL(cudaBindTexture(0, tex_xtimee,d_xtimee  ));
	CUDA_SAFE_CALL(cudaBindTexture(0, tex_xtimeb,d_xtimeb  ));
	CUDA_SAFE_CALL(cudaBindTexture(0, tex_xtimed,d_xtimed  ));
	CUDA_SAFE_CALL(cudaBindTexture(0, tex_xtime9,d_xtime9  ));

	CUDA_SAFE_CALL( cudaMemcpyToSymbol( "_Sbox",       Sbox,       256 ) );
	CUDA_SAFE_CALL( cudaMemcpyToSymbol( "_InvSbox",    InvSbox,    256 ) );
	CUDA_SAFE_CALL( cudaMemcpyToSymbol( "_Xtime2Sbox", Xtime2Sbox, 256 ) );
	CUDA_SAFE_CALL( cudaMemcpyToSymbol( "_Xtime3Sbox", Xtime3Sbox, 256 ) );
	CUDA_SAFE_CALL( cudaMemcpyToSymbol( "_Xtime2",     Xtime2,     256 ) );
	CUDA_SAFE_CALL( cudaMemcpyToSymbol( "_Xtime9",     Xtime9,     256 ) );
	CUDA_SAFE_CALL( cudaMemcpyToSymbol( "_XtimeB",     XtimeB,     256 ) );
	CUDA_SAFE_CALL( cudaMemcpyToSymbol( "_XtimeD",     XtimeD,     256 ) );
	CUDA_SAFE_CALL( cudaMemcpyToSymbol( "_XtimeE",     XtimeE,     256 ) );


	  /* pre-allocating device memory */
  	CUDA_SAFE_CALL( cudaMalloc((void **) &d_in_enc,  MAX_BLOCK_SIZE  ) );
	CUDA_SAFE_CALL( cudaMalloc((void **) &d_out_enc, MAX_BLOCK_SIZE  ) );
   
	CUDA_SAFE_CALL( cudaMalloc( (void **) &d_in_dec,  MAX_BLOCK_SIZE  ) );
	CUDA_SAFE_CALL( cudaMalloc( (void **) &d_out_dec, MAX_BLOCK_SIZE ) );



	CUDA_SAFE_CALL( cudaMalloc( (void **) &d_expkey, 4 * Nb * (Nr + 1) ) );

	CUDA_SAFE_CALL( cudaMallocHost((void**) &h_in_enc,MAX_BLOCK_SIZE) );
	
	CUDA_SAFE_CALL( cudaMallocHost((void**) &h_out_enc,MAX_BLOCK_SIZE) );
	
	CUDA_SAFE_CALL( cudaMallocHost((void**) &h_in_dec,MAX_BLOCK_SIZE) );
	
	CUDA_SAFE_CALL( cudaMallocHost((void**) &h_out_dec,MAX_BLOCK_SIZE) );
}

 __host__ int gpu_device_count()
{
	int deviceCount = 0;

	if (cudaGetDeviceCount(&deviceCount) != cudaSuccess) {
		return 0;
	}

   	
	return deviceCount;

	/*check devideQuesry api of sdk for more support*/

}

 __host__ int  gpu_getMaxThreadCount()
{
     cudaDeviceProp deviceProp;
     cudaGetDeviceProperties(&deviceProp, 0);

	  return deviceProp.maxThreadsPerBlock;
}

 __host__ void gpu_exit()
{
	if(d_in_enc)
		CUDA_SAFE_CALL( cudaFree(d_in_enc) );
	if(d_out_enc)
		CUDA_SAFE_CALL( cudaFree(d_out_enc) );

	if(d_in_dec)
		CUDA_SAFE_CALL( cudaFree(d_in_dec) );
	if(d_out_dec)
		CUDA_SAFE_CALL( cudaFree(d_out_dec) );
	if(d_expkey)
		CUDA_SAFE_CALL( cudaFree(d_expkey) );
	if(d_sbox)
		CUDA_SAFE_CALL( cudaFree(d_sbox) );
	if(d_invsbox)
		CUDA_SAFE_CALL( cudaFree(d_invsbox) );

	if(d_xtimee)
		CUDA_SAFE_CALL( cudaFree(d_xtimee) );
	if(d_xtimeb)
		CUDA_SAFE_CALL( cudaFree(d_xtimeb) );
	if(d_xtimed)
		CUDA_SAFE_CALL( cudaFree(d_xtimed) );
	if(d_xtime9)
		CUDA_SAFE_CALL( cudaFree(d_xtime9) );
	cudaFreeHost(h_in_enc);
	cudaFreeHost(h_out_enc);
	cudaFreeHost(h_in_dec);
	cudaFreeHost(h_out_dec);
}

 __host__ int gpu_set_key(uchar* key )
{

	uchar expkey[4 * Nb * (Nr + 1)];
	ExpandKey( (uchar*)key, (uchar*)expkey );
        CUDA_SAFE_CALL( cudaMemcpy( (void*)d_expkey, (void*)expkey, 4 * Nb * (Nr + 1), cudaMemcpyHostToDevice ) );

	return 0;
}
