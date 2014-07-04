#include <stdio.h>
#include <err.h>
#include <string.h>
#include "timer.h"
extern "C"{
#include "aes_gpu.h"
}
#define BUF_SIZE 8192

#include "bmploader.h"

#include <stdint.h>
#include <gcrypt.h>

#define AES128_KEY_LEN AES_KEY_SIZE
#define AES_BLOCK_LEN AES128_KEY_LEN

#define TRIALS 100

const uint8_t AES128_KEY[AES128_KEY_LEN] = \
   {0x01, 0x23, 0x45, 0x67, 0x89, 0xAB, 0xCD, 0xEF,     \
    0x01, 0x23, 0x45, 0x67, 0x89, 0xAB, 0xCD, 0xEF};

uint8_t test_ctr[AES_BLOCK_LEN] =\
    {0xaf, 0x88, 0x0c, 0xb9, 0x7c, 0xa2, 0xa8, 0x6d,\
     0x2d, 0xef, 0xef, 0x54, 0xc5, 0x03, 0xde, 0x8b}; 

int main(int argc, char** argv)
{

	if (argc!=2) { fprintf(stderr,"%s <file name>\n",argv[0]); return -1; }
	
	cpu_init();
	gpu_init(argc,argv);


	
	uchar* key=(uchar*)AES128_KEY;
	bigint iv;
	memcpy(&iv,&test_ctr,sizeof(test_ctr));
	cpu_set_key((uchar*) key);
	gpu_set_key((uchar*) key);


	uchar4* dst;
	uchar4* src,*src1;

	int width, height;
	char *name=argv[1];


	LoadBMPFile(&dst, &width, &height,name);
	uint size= width*height*sizeof(uchar4);

	src=(uchar4*)malloc(size);
	src1=(uchar4*)malloc(size);
	if (!src || !src1) { fprintf(stderr,"Out of mem\n"); exit(-1);}
	
	double ts=0;
	double ts_cpu=0;
	double ts_gpu=0;
	double total_time=0;
	double total_time_cpu=0;
	double total_time_gpu=0;

	for( int i=0;i<TRIALS;i++){
		memcpy(src,dst,size);
	
		/* In place encryption of the data */
		ts=gpu_timestamp();
		if (cpu_encrypt_string_count((uchar*)src,size,iv)!=0)
		     errx(1, "gcry_cipher_encrypt");
		ts_cpu=gpu_timestamp();
		gpu_decrypt_string((uchar*)src,size,(uchar**)&src1,iv,false);
		ts_gpu=gpu_timestamp();
		total_time+=(ts_gpu-ts);
		total_time_cpu+=(ts_cpu-ts);
		total_time_gpu+=(ts_gpu-ts_cpu);
		
		for(int i=0;i<size;i++){
			if (((uchar*)src1)[i]!=((uchar*)dst)[i]){
				fprintf(stderr,"error in %i\n",i);
				break;
			}
		}

	}

	aes_cpu_exit();
	gpu_exit();
	
	fprintf(stderr,"Time per iteration: %.3f us, CPU time: %.3f us, GPU time %.3f us:\n", total_time/TRIALS,total_time_cpu/TRIALS, total_time_gpu/TRIALS );
	return 0;
}
	

/* Test usage of gcrypt for AES CTR mode.  More things would need to
   be done to do this securely for an application (e.g. ensure that
   gcrypt handles are closed to zeroize information), but that's not
   our purpose here. */
#if 0

#include <gcrypt.h>
#include <stdio.h>
#include <stdint.h>
#include <err.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

#include "crypto.h"

#define PAGE_SIZE 4096

void usage(const char *prog_name)
{
   printf("Usage: %s <out filename>\n", prog_name);
}

/* A way to put in an explicit test vector, perhaps eventually read
   from file? */
/* uint8_t test_ctr[AES_BLOCK_LEN] = \ */
/*    {0xaf, 0x88, 0x0c, 0xb9, 0x7c, 0xa2, 0xa8, 0x6d, \ */
/*     0x2d, 0xef, 0xef, 0x54, 0xc5, 0x03, 0xde, 0x8b}; */

int main(int argc, const char *argv[])
{
   int out_fd;
   gcry_cipher_hd_t hd;
   uint8_t ctr[AES_BLOCK_LEN];

   const size_t data_len = PAGE_SIZE;
   uint8_t data[data_len];

   if (argc != 2) {
      usage(argv[0]);
      exit(1);
   }

   if ((out_fd = open(argv[1], O_RDWR | O_CREAT | O_TRUNC, S_IRUSR | S_IWUSR)) < 0)
      err(1, "open");

   /* Get random counter */
   gcry_randomize(ctr, sizeof(ctr), GCRY_STRONG_RANDOM);
   /* memcpy(ctr, test_ctr, AES_BLOCK_LEN); */

   /* Prepare the cipher */
   if (gcry_cipher_open(&hd, GCRY_CIPHER_AES256, GCRY_CIPHER_MODE_CTR, 0) != 0)
      errx(1, "gcry_cipher_open");

   if (gcry_cipher_setkey(hd, AES256_KEY, AES256_KEY_LEN) != 0)
      errx(1, "gcry_cipher_setkey");

   if (gcry_cipher_setctr(hd, ctr, sizeof(ctr)) != 0)
      errx(1, "gcry_cipher_setctr");

   if (write(out_fd, ctr, sizeof(ctr)) != sizeof(ctr))
      err(1, "write");

   /* Prepare the data */
   /* We use a page of zeros, because this is what we may well be
      getting from the kernel and then xoring with data as it comes
      in */
   memset(data, 0, data_len);

   /* In place encryption of the data */
   if (gcry_cipher_encrypt(hd, (unsigned char *)data, data_len, NULL, 0) != 0)
      errx(1, "gcry_cipher_encrypt");

   if (write(out_fd, data, data_len) != data_len)
      err(1, "write");

   close(out_fd);

   return 0;
}
#endif
