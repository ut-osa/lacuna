#ifndef PRIV_CRYPT_INT_H
#define PRIV_CRYPT_INT_H

#include <stdlib.h>
#include <stdint.h>

struct priv_user_ops;

struct priv_cctx {
   uint32_t alg;
   uint32_t key_size, iv_size;
   uint8_t iv[MAX_IV_LEN];
   uint8_t dir;

   struct priv_user_ops *user_ops;
   void *user_ctx;

   struct priv_backend_ops *backend_ops;
   void *backend_ctx;

   struct priv_kex_ops *kex_ops;
   struct priv_rng_ops *rng_ops;
};

struct priv_user_ops {
   int (*init)(void **result, unsigned int alg);
   /* key, iv len in bytes */
   int (*set_key)(void *user_ctx, size_t len, uint8_t *key);
   int (*set_iv)(void *user_ctx, size_t len, uint8_t *iv);
   /* in place {en,de}cryption */
   int (*encrypt)(void *user_ctx, size_t len, uint8_t *data);   
   int (*decrypt)(void *user_ctx, size_t len, uint8_t *data);
   void (*destroy)(void *user_ctx);
};

#define PRIV_BACKEND_KERN_GET_ID 1

struct priv_backend_ops {
   int (*init)(void **result, struct priv_cctx *cctx);
   int (*set_iv)(void *backend_ctx, size_t len, uint8_t *iv);
   int (*send_kex_msg)(void *backend_ctx,
                       size_t *out_size, uint8_t *out_buf,
                       size_t in_size, uint8_t *in_buf);
   int (*set_activation)(void *backend_ctx, int activation);
   void (*destroy)(void *backend_ctx);

   /* Debugging function that causes the remote end to do the
      {en,de}cryption directly. Not required for all backends to
      define this function (can be left NULL). */
   int (*crypt)(void *backend_ctx, size_t len, uint8_t *buf);

   /* Special per-backend requests */
   int (*per_backend)(void *backend_ctx, int req, void *req_data);
};

struct priv_rng_ops {
   void (*get_random_bytes)(size_t len, uint8_t *buf);
};

struct priv_kex_ops {
   int (*init)(void **result, size_t key_size);
   int (*get_key)(void *kex_ctx, uint8_t *key_buf);
   /* rc == 0 -> no more messages to be sent
    * rc < 0 -> error
    */
   int (*process_msg)(void *kex_ctx,
                      size_t *out_len, uint8_t *out_buf,
                      size_t in_len, uint8_t *in_buf);
   int (*destroy)(void *kex_ctx);
};

extern struct priv_user_ops gcrypt_priv_user_ops;
extern struct priv_rng_ops gcrypt_priv_rng_ops;

extern struct priv_user_ops aesni_priv_user_ops;

extern struct priv_kex_ops null_kex_ops;
extern struct priv_kex_ops test_kex_ops;
extern struct priv_kex_ops polarssl_kex_ops;

extern struct priv_backend_ops kern_backend_ops;
extern struct priv_backend_ops gpu_backend_ops;

void print_hex(size_t len, uint8_t *buf);

#endif
