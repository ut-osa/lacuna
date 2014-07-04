#include <stdio.h>
#include <stdint.h>
#include <string.h>

#include <priv_crypt.h>
#include "priv_crypt_int.h"

/* Matches kernel crypto_aes_ctx */
struct aes_ctx {
   uint8_t key_enc[240];
   uint8_t key_dec[240];
   uint32_t key_len;
};

extern int aesni_set_key(struct aes_ctx *ctx, const uint8_t *in_key,
                         unsigned int key_len);

extern void aesni_ctr_enc(struct aes_ctx *ctx, const uint8_t *dst, uint8_t *src,
                          size_t len, uint8_t *iv);

struct aesni_state {
   struct aes_ctx *aesni_ctx;
   /* aes_mem_region points to the start of the aesni_ctx block so
      that freeing can occur */
   void *aes_mem_region;
   /* XXX: alignment requirements for this IV? */
   uint8_t iv[16];
};

static int priv_aesni_init(void **result, unsigned int alg)
{
   int rc = -PRIV_CRYPT_SOME_ERR;
   struct aesni_state *st;
   uint8_t *aes_ctx_buf;

   if (!result)
      goto out;

   st = malloc(sizeof(struct aesni_state));
   if (!st)
      goto out;

   /* XXX: could support other AES modes... */
   if (alg != PRIV_CRYPT_ALG_AES_CTR) {
      rc = -PRIV_CRYPT_NO_ALG;
      goto free;
   }

   aes_ctx_buf = malloc(2*sizeof(struct aes_ctx));
   if (!aes_ctx_buf)
      goto free;

   st->aes_mem_region = aes_ctx_buf;
   st->aesni_ctx = (struct aes_ctx*)((((unsigned long)aes_ctx_buf) + 15) & ~15);

   *result = st;
   rc = 0;
   goto out;

 free:
   free(st);
 out:
   return rc;
}

static int priv_aesni_set_key(void *user_ctx, size_t len, uint8_t *key)
{
   int rc = -PRIV_CRYPT_SOME_ERR;
   struct aesni_state *st = user_ctx;
   
   if (!user_ctx)
      goto out;

   if (aesni_set_key(st->aesni_ctx, key, len))
      goto out;

   rc = 0;

 out:
   return rc;
}

static int priv_aesni_set_iv(void *user_ctx, size_t len, uint8_t *iv)
{
   int rc = -PRIV_CRYPT_SOME_ERR;
   struct aesni_state *st = user_ctx;

   if (!user_ctx)
      goto out;

   if (len != sizeof(st->iv))
      goto out;

   memcpy(st->iv, iv, len);

   rc = 0;
 out:
   return rc;
}

static int priv_aesni_crypt(void *user_ctx, size_t len, uint8_t *data, int enc)
{
   int rc = -PRIV_CRYPT_SOME_ERR;
   struct aesni_state *st = user_ctx;
   uint8_t last[16];

   if (!user_ctx)
      goto out;

   /* the AES-NI code doesn't seem to encrypt partial blocks */
   if (len > 16) {
      size_t most_len = len - len % 16;
      aesni_ctr_enc(st->aesni_ctx, data, data, most_len, st->iv);
      data += most_len;
      len -= most_len;
   }
   if (len > 0) {
      memcpy(last, data, len);
      aesni_ctr_enc(st->aesni_ctx, last, last, 16, st->iv);
      memcpy(data, last, len);
   }

   rc = 0;
 out:
   return rc;
}

static int priv_aesni_encrypt(void *user_ctx, size_t len, uint8_t *data)
{
   return priv_aesni_crypt(user_ctx, len, data, 1);
}

static int priv_aesni_decrypt(void *user_ctx, size_t len, uint8_t *data)
{
   return priv_aesni_crypt(user_ctx, len, data, 0);
}

static void priv_aesni_destroy(void *user_ctx)
{
   struct aesni_state *st = user_ctx;

   if (!user_ctx)
      return;

   /* zeroize */
   memset(st->aesni_ctx, 0, sizeof(st->aesni_ctx));

   free(st->aes_mem_region);
}

struct priv_user_ops aesni_priv_user_ops = {
   .init = priv_aesni_init,
   .set_key = priv_aesni_set_key,
   .set_iv = priv_aesni_set_iv,
   .decrypt = priv_aesni_decrypt,
   .encrypt = priv_aesni_encrypt,
   .destroy = priv_aesni_destroy,
};
