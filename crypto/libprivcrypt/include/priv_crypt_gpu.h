#ifndef PRIV_CRYPT_GPU_H
#define PRIV_CRYPT_GPU_H

#include <stdint.h>

/* XXX: Unify with kernel structures? */

struct priv_crypt_gpu_alg_desc {
   uint32_t alg;
   uint32_t key_size;
}  __attribute__((packed));

struct priv_crypt_gpu_iv_desc {
   uint32_t iv_size;
   uint8_t iv[MAX_IV_LEN];
} __attribute__((packed));

struct priv_crypt_gpu_kex_desc {
   uint32_t size;
   uint8_t buf[0];
} __attribute__((packed));

struct priv_crypt_gpu_cmd_desc {
   uint8_t cmd;
   union {
      struct priv_crypt_gpu_alg_desc alg;
      struct priv_crypt_gpu_iv_desc iv;
      struct priv_crypt_gpu_kex_desc kex;
      /* some commands have no further data, in which case ignore this
         part */
   } desc;
} __attribute__((packed));

#endif
