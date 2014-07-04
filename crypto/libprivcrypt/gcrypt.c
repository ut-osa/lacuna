#include <gcrypt.h>
#include <stdio.h>

#include <priv_crypt.h>
#include "priv_crypt_int.h"

/* XXX: bring this in from priv_crypt_kern? */
#define MAX_IV_LEN 64

//#define DEBUG_GCRYPT

#ifdef DEBUG_GCRYPT
#define DPRINTF(fmt, args...) fprintf(stderr, fmt, args)
#else
#define DPRINTF(fmt, args...) do {} while(0);
#endif

struct gcrypt_state {
   int alg, mode;
   size_t block_sz;
   gcry_cipher_hd_t hd;
};

static int gcrypt_init(void **result, unsigned int alg)
{
   int rc = -PRIV_CRYPT_SOME_ERR, gcry_alg, mode;
   struct gcrypt_state *st;

   if (!result)
      goto out;

   st = malloc(sizeof(struct gcrypt_state));
   if (!st) {
      rc = -PRIV_CRYPT_SOME_ERR;
      goto out;
   }

   switch (alg) {
   case PRIV_CRYPT_ALG_AES_CTR:
      gcry_alg = GCRY_CIPHER_AES256;
      mode = GCRY_CIPHER_MODE_CTR;
      st->block_sz = 16;
      break;
   default:
      rc = -PRIV_CRYPT_NO_ALG;
      goto free;
   }

   /* Assuming the block size is <= the IV size, and we've defined a
      maximum IV size elsewhere */
   if (st->block_sz > MAX_IV_LEN) {
      rc = -PRIV_CRYPT_SOME_ERR;
      goto free;
   }

   if ((rc = gcry_cipher_open(&st->hd, gcry_alg, mode, 0)) != 0) {
      DPRINTF("gcry_cipher_open: %d\n", rc);
      rc = -PRIV_CRYPT_SOME_ERR;
      goto free;
   }

   st->alg = gcry_alg;
   st->mode = mode;

   *result = st;
   rc = 0;
   goto out;

 free:
   free(st);

 out:
   return rc;
}

static int gcrypt_set_key(void *user_ctx, size_t len, uint8_t *key)
{
   int rc = -PRIV_CRYPT_SOME_ERR;
   struct gcrypt_state *st = user_ctx;

   if (!user_ctx)
      goto out;

   if ((rc = gcry_cipher_setkey(st->hd, key, len)) != 0) {
      DPRINTF("gcry_cipher_setkey: %d\n", rc);
      rc = -PRIV_CRYPT_SOME_ERR;
      goto out;
   }

   rc = 0;

 out:
   return rc;
}

static int gcrypt_set_iv(void *user_ctx, size_t len, uint8_t *iv)
{
   int rc = -PRIV_CRYPT_SOME_ERR;
   struct gcrypt_state *st = user_ctx;

   if (!user_ctx)
      goto out;

   if (st->mode != GCRY_CIPHER_MODE_CTR) {
      if ((rc = gcry_cipher_setiv(st->hd, iv, len)) != 0) {
         DPRINTF("gcry_cipher_setiv: %d\n", rc);
         rc = -PRIV_CRYPT_SOME_ERR;
         goto out;
      }
   } else {
      if ((rc = gcry_cipher_setctr(st->hd, iv, len)) != 0) {
         DPRINTF("gcry_cipher_setctr: %d\n", rc);
         rc = -PRIV_CRYPT_SOME_ERR;
         goto out;
      }
   }

   rc = 0;

 out:
   return rc;
}

static int gcrypt_do_crypt(gcry_cipher_hd_t hd, size_t len, uint8_t *data,
                           int enc)
{
   int rc = -PRIV_CRYPT_SOME_ERR;

   if (enc)
      rc = gcry_cipher_encrypt(hd, (unsigned char *)data, len, NULL, 0);
   else
      rc = gcry_cipher_decrypt(hd, (unsigned char *)data, len, NULL, 0);
   if (rc != 0) {
      DPRINTF("gcry_cipher_%scrypt: %d\n",
              (enc ? "en" : "de"), rc);
      rc = -PRIV_CRYPT_SOME_ERR;
      goto out;
   }
   rc = 0;

 out:
   return rc;
}

static int gcrypt_crypt(void *user_ctx, size_t len, uint8_t *data, int enc)
{
   int rc = -PRIV_CRYPT_SOME_ERR;
   struct gcrypt_state *st = user_ctx;

   if (!user_ctx)
      goto out;

   rc = gcrypt_do_crypt(st->hd, len, data, enc);
   if (rc < 0)
      goto out;
#ifdef CONFIG_GCRYPT_REAL_STREAM
   if (st->mode == GCRY_CIPHER_MODE_CTR && (len % st->block_sz)) {
      /* The kernel increments the CTR-mode counter if the last block
         is not of a multiple of the block length. It seems that
         libgcrypt does not. */
      /* If we switch to the "CTR-mode pad" version, then we won't
         want this behavior */
      size_t leftover;
      unsigned char extra[MAX_IV_LEN];

      leftover = st->block_sz - (len % st->block_sz);
      DPRINTF("gcrypt: leftover = %lu\n", leftover);
      rc = gcrypt_do_crypt(st->hd, leftover, extra, enc);
      if (rc < 0)
         goto out;
   }
#endif

 out:
   return rc;
}

static int gcrypt_encrypt(void *user_ctx, size_t len, uint8_t *data)
{
   return gcrypt_crypt(user_ctx, len, data, 1);
}

static int gcrypt_decrypt(void *user_ctx, size_t len, uint8_t *data)
{
   return gcrypt_crypt(user_ctx, len, data, 0);
}

static void gcrypt_destroy(void *user_ctx)
{
   struct gcrypt_state *st = user_ctx;

   if (!user_ctx)
      return;

   gcry_cipher_close(st->hd);

   free(user_ctx);
}

static void gcrypt_get_random_bytes(size_t len, uint8_t *buf)
{
   gcry_randomize(buf, len, GCRY_STRONG_RANDOM);
}

struct priv_user_ops gcrypt_priv_user_ops = {
   .init = gcrypt_init,
   .set_key = gcrypt_set_key,
   .set_iv = gcrypt_set_iv,
   .decrypt = gcrypt_decrypt,
   .encrypt = gcrypt_encrypt,
   .destroy = gcrypt_destroy,
};

struct priv_rng_ops gcrypt_priv_rng_ops = {
   .get_random_bytes = gcrypt_get_random_bytes,
};
