#include <stdio.h>
#include <linux/limits.h>
#include <unistd.h>
#include <string.h>
#include <stdint.h>
#include <errno.h>

#include <priv_crypt.h>
#include <priv_crypt_kern.h>

#include "priv_crypt_int.h"

#define DEBUG 1

#ifdef DEBUG
#define DPRINTF(fmt, args...) fprintf(stderr, fmt, ##args)
#else
#define DPRINTF(fmt, args...) do {} while(0)
#endif

int priv_set_option(struct priv_cctx *cctx, uint32_t option)
{
   int rc = -PRIV_CRYPT_SOME_ERR;

   switch (option) {
   case PRIV_OPTION_TEST_KEX:
      cctx->kex_ops = &test_kex_ops;
      rc = 0;
      break;
   case PRIV_OPTION_DIR_DEC:
      cctx->dir = PRIV_CRYPT_DIR_DEC;
      rc = 0;
      break;
   default:
      break;
   }

   return rc;
}

static void
setup_cctx_ops(struct priv_cctx *cctx)
{
   cctx->backend_ops = &kern_backend_ops;
#ifdef USE_AES_NI
   cctx->user_ops = &aesni_priv_user_ops;
#else
   cctx->user_ops = &gcrypt_priv_user_ops;
#endif
   cctx->kex_ops = &polarssl_kex_ops;
   cctx->rng_ops = &gcrypt_priv_rng_ops;
}

static size_t get_iv_size(uint32_t alg)
{
   if (alg == PRIV_CRYPT_ALG_AES_CTR)
      return 16;

   return -1;
}

int priv_create_cctx(struct priv_cctx **result,
                     uint32_t alg, uint32_t key_size)
{
   int i, rc = -PRIV_CRYPT_SOME_ERR;
   struct priv_cctx *cctx = NULL;
   uint8_t key[MAX_KEY_LEN];

   if (!result)
      goto out;

   *result = NULL;

   if (!priv_crypt_alg_valid(alg)) {
      rc = -PRIV_CRYPT_NO_ALG;
      goto out;
   }

   cctx = malloc(sizeof(struct priv_cctx));
   if (!cctx)
      goto out;

   setup_cctx_ops(cctx);

   cctx->alg = alg;
   if ((rc = cctx->user_ops->init(&cctx->user_ctx, cctx->alg)) < 0)
      goto err_free;

   cctx->key_size = key_size;
   cctx->dir = PRIV_CRYPT_DIR_ENC;

   cctx->iv_size = get_iv_size(cctx->alg);
   cctx->rng_ops->get_random_bytes(cctx->iv_size, cctx->iv);
   if ((rc = cctx->user_ops->set_iv(cctx->user_ctx, cctx->iv_size, cctx->iv)) < 0)
      goto err_free;

   rc = 0;
   *result = cctx;
   goto out;

 err_free:
   free(cctx);
   cctx = NULL;

 out:
   return rc;
}

void priv_destroy_cctx(struct priv_cctx *cctx)
{
   if (cctx->backend_ctx)
      cctx->backend_ops->destroy(cctx->backend_ctx);
   cctx->user_ops->destroy(cctx->user_ctx);
}

static int do_kex(struct priv_cctx *cctx)
{
   int rc = -PRIV_CRYPT_SOME_ERR;
   void *kex_ctx;
   uint8_t kex_buf[MAX_KEX_MSG_LEN], key_buf[MAX_KEY_LEN];
   size_t kex_in_len = 0, kex_out_len;

   if (cctx->kex_ops->init(&kex_ctx, cctx->key_size) < 0)
      goto out;

   do {
      u32 tmp;
      rc = cctx->kex_ops->process_msg(kex_ctx,
                                      &kex_out_len, kex_buf,
                                      kex_in_len, kex_buf);
      DPRINTF("%s: process_msg: rc %d\n",
              __func__, rc);
      if (rc < 1)
         break;
      rc = cctx->backend_ops->send_kex_msg(cctx->backend_ctx,
                                           &kex_in_len, kex_buf,
                                           kex_out_len, kex_buf);
      DPRINTF("%s: backend kex: rc %d\n",
              __func__, rc);

   } while (rc >= 0);

   if (rc == 0) {
      rc = cctx->kex_ops->get_key(kex_ctx, key_buf);
   }
   cctx->kex_ops->destroy(kex_ctx);
   if (rc == 0) {
      rc = cctx->user_ops->set_key(cctx->user_ctx,
                                   cctx->key_size,
                                   key_buf);
   }

 out:
   memset(kex_buf, 0, sizeof(kex_buf));
   memset(key_buf, 0, sizeof(key_buf));
   return rc;
}

int priv_new_backend(struct priv_cctx *cctx)
{
   int rc;

   rc = cctx->backend_ops->init(&cctx->backend_ctx, cctx);
   if (rc < 0)
      goto out;

   if ((rc = do_kex(cctx)) < 0) {
      DPRINTF("do_kex failed (rc=%d,errno=%d)\n",
              rc, errno);
      goto out_destroy;
   }

   rc = cctx->backend_ops->set_iv(cctx->backend_ctx, cctx->iv_size, cctx->iv);
   if (rc < 0)
      goto out_destroy;

   rc = 0;
   goto out;

 out_destroy:
   cctx->backend_ops->destroy(cctx->backend_ctx);
   cctx->backend_ctx = NULL;
 out:
   return rc;
}

int priv_get_id(struct priv_cctx *cctx, priv_crypt_id_t *id)
{
   int rc = -PRIV_CRYPT_SOME_ERR;

   if (cctx->backend_ops != &kern_backend_ops)
      goto out;
   if (!cctx->backend_ops->per_backend)
      goto out;

   if (cctx->backend_ops->per_backend(cctx->backend_ctx,
                                      PRIV_BACKEND_KERN_GET_ID,
                                      id) < 0)
      goto out;

   rc = 0;
 out:
   return rc;
}

/* Pipe some text to the remote end and use that end for performing
   {en,de}cryption. */
/* Strictly a debugging function; won't work without certain
   kernel-side features on. */
int priv_remote_crypt(struct priv_cctx *cctx, size_t len, uint8_t *data)
{
   int rc = -PRIV_CRYPT_SOME_ERR;

   if (!cctx->backend_ops->crypt)
      goto out;

   if (cctx->backend_ops->crypt(cctx->backend_ctx, len, data) < 0)
      goto out;

   rc = 0;
 out:
   return rc;
}

int priv_crypt(struct priv_cctx *cctx, size_t len, uint8_t *data)
{
   int rc;

   /* do the opposite of what would be done on the remote side */
   if (cctx->dir == PRIV_CRYPT_DIR_ENC)
      rc = cctx->user_ops->decrypt(cctx->user_ctx, len, data);
   else
      rc = cctx->user_ops->encrypt(cctx->user_ctx, len, data);

   return rc;
}

int priv_activate(struct priv_cctx *cctx)
{
   return cctx->backend_ops->set_activation(cctx->backend_ctx, 1);
}

int priv_deactivate(struct priv_cctx *cctx)
{
   return cctx->backend_ops->set_activation(cctx->backend_ctx, 0);
}
