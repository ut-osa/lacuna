// advanced encryption standard
// author: karl malbrain, malbrain@yahoo.com

/*
This work, including the source code, documentation
and related data, is placed into the public domain.

The orginal author is Karl Malbrain.

THIS SOFTWARE IS PROVIDED AS-IS WITHOUT WARRANTY
OF ANY KIND, NOT EVEN THE IMPLIED WARRANTY OF
MERCHANTABILITY. THE AUTHOR OF THIS SOFTWARE,
ASSUMES _NO_ RESPONSIBILITY FOR ANY CONSEQUENCE
RESULTING FROM THE USE, MODIFICATION, OR
REDISTRIBUTION OF THIS SOFTWARE.
*/

#ifndef _AES_H_INC_
#define _AES_H_INC_


#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CUDA_SAFE_CALL(x) if((x)!=cudaSuccess) { fprintf(stderr,"CUDA ERROR %d %s\n", __LINE__, cudaGetErrorString(cudaGetLastError())); exit(-1); }

typedef long long __int64;
typedef unsigned char uchar;


// AES only supports Nb=4
#define Nb 4			// number of columns in the state & expanded key
#define Nk 4			// number of columns in a key
#define Nr 10			// number of rounds in encryption

#define AES_KEY_SIZE 16

typedef struct {
	uchar Sbox[256];
	uchar InvSbox[256];
	uchar Xtime2Sbox[256];
	uchar Xtime3Sbox[256];
	uchar Xtime2[256];
	uchar Xtime9[256];
	uchar XtimeB[256];
	uchar XtimeD[256];
	uchar XtimeE[256];
} t_global;



typedef unsigned int uint;

#define MAXVAL 0xFF
struct bigint{
	uint d[4];
};

struct bigint plus(struct bigint x, uint y);        


extern uchar Sbox[256];
extern uchar InvSbox[256];
extern uchar Xtime2Sbox[256];
extern uchar Xtime3Sbox[256];
extern uchar Xtime2[256];
extern uchar Xtime9[256];
extern uchar XtimeB[256];
extern uchar XtimeD[256];
extern uchar XtimeE[256];


/* ============== Interface subsection =============*/

void ExpandKey (uchar *key, uchar *expKey);
 

int gpu_encrypt_string(uchar* in, int in_len, uchar** out, struct bigint iv,bool);
int gpu_decrypt_string(uchar* in, int in_len, uchar** out, struct bigint iv,bool);

int gpu_device_count(void);
int gpu_set_key(uchar* key);
int  gpu_getMaxThreadCount(void);
void gpu_init(int argc, char** argv);
void gpu_exit(void);



void cpu_init(void);
void aes_cpu_exit(void);
int cpu_encrypt_string_count(uchar* in, int in_len, struct bigint iv);
int cpu_decrypt_string_count(uchar* in, int in_len, struct bigint iv);
int cpu_set_key(uchar* key);

double gpu_timestamp(void);

#define NO_ENCRYPT_ENV "NO_ENCRYPT"
#endif // _AES_H_INC_
