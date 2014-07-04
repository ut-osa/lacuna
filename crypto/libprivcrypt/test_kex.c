#include <stdio.h>
#include <string.h>

#include <priv_crypt.h>
#include "priv_crypt_int.h"

struct test_kex_ctx {
   int msg_done;
};

static int test_init(void **result, size_t key_size)
{
   int rc = -PRIV_CRYPT_SOME_ERR;
   struct test_kex_ctx *ctx;
   
   printf("%s\n", __func__);

   *result = NULL;

   ctx = malloc(sizeof(*ctx));
   if (!ctx)
      goto out;

   ctx->msg_done = 0;
   *result = ctx;
   rc = 0;

 out:
   return rc;
}

static int test_get_key(void *kex_ctx, uint8_t *key_buf)
{
   printf("%s\n", __func__);

   return -1;
}

static int test_process_msg(void *kex_ctx,
                            size_t *out_len, uint8_t *out_buf,
                            size_t in_len, uint8_t *in_buf)
{
   int rc = -1;
   const char test_msg[] = "This is a test.";
   struct test_kex_ctx *ctx = kex_ctx;

   printf("%s\n", __func__);

   if (ctx->msg_done) {
      size_t len = *out_len;

      if (len < 1) {
         rc = -1;
         goto out;
      }

      if (*out_buf != PRIV_CRYPT_KEX_DEBUG) {
         rc = -1;
         goto out;
      }

      print_hex(len - 1, out_buf + 1);

      rc = 0;
      goto out;
   }

   *out_buf = PRIV_CRYPT_KEX_DEBUG;
   memcpy(out_buf + sizeof(uint8_t), test_msg, strlen(test_msg));

   *out_len = sizeof(uint8_t) + strlen(test_msg);
   ctx->msg_done = 1;

   rc = 1;
 out:
   printf("%s: %d\n", __func__, rc);
   return rc;
}

static int test_destroy(void *kex_ctx)
{
   printf("%s\n", __func__);

   free(kex_ctx);
   return 0;
}

struct priv_kex_ops test_kex_ops = {
   .init = test_init,
   .get_key = test_get_key,
   .process_msg = test_process_msg,
   .destroy = test_destroy,
};
