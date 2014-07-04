#include <priv_crypt.h>
#include "priv_crypt_int.h"

struct null_kex_ctx {
   size_t key_size;
};

static int null_init(void **result, size_t key_size)
{
   int rc = -PRIV_CRYPT_SOME_ERR;
   struct null_kex_ctx *ctx;

   *result = NULL;

   ctx = malloc(sizeof(*ctx));
   if (!ctx)
      goto out;

   ctx->key_size = key_size;

   *result = ctx;
   rc = 0;

 out:
   return rc;
}

static int null_get_key(void *kex_ctx, uint8_t *key_buf)
{
   struct null_kex_ctx *ctx = kex_ctx;
   size_t i;

   for (i = 0; i < ctx->key_size; i++) {
      key_buf[i] = (uint8_t)i;
   }

   return 0;
}

static int null_process_msg(void *kex_ctx,
                            size_t *out_len, uint8_t *out_buf,
                            size_t in_len, uint8_t *in_buf)
{
   return 0;
}

static int null_destroy(void *kex_ctx)
{
   free(kex_ctx);
   return 0;
}

struct priv_kex_ops null_kex_ops = {
   .init = null_init,
   .get_key = null_get_key,
   .process_msg = null_process_msg,
   .destroy = null_destroy,
};
