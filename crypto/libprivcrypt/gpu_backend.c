#include <stdio.h>
#include <stdint.h>
#include <string.h>

#include <priv_crypt.h>
#include <priv_crypt_gpu.h>
#include "priv_crypt_int.h"

/* This maximum size includes any headers */
#define MAX_CMD_LEN MAX_KEX_MSG_LEN

#define ERROR(fmt, args...) fprintf(stderr, fmt, ##args)

struct gpu_backend {
   /*
    * format of buf:
    * | cmd (MAX_CMD_LEN) | response (MAX_CMD_LEN) |
    */
   uint8_t *buf;
   size_t buf_size;
   /* XXX: put other GPU state - i.e. state related to how data will
      be sent to GPU - here */
};

static int setup_gpu(struct gpu_backend *backend)
{
   /* XXX: Mark: do your magic GPU setup here, storing any necessary
      data in the backend variable (you can add state if you need
      it) */
   ERROR("%s unimplemented\n", __func__);
   return -PRIV_CRYPT_SOME_ERR;
}

static int invoke_gpu(struct gpu_backend *backend)
{
   /* XXX: Mark: the command data is in backend->buf (check the
      formatting above), invoke the GPU and put the response in the
      buffer */
   ERROR("%s unimplemented\n", __func__);
   return -PRIV_CRYPT_SOME_ERR;
}

/* XXX: Mark: any constraints on the memory to be transferred to the
   GPU? */
static int alloc_gpu_memory(struct gpu_backend *backend, size_t size)
{
   int rc = -PRIV_CRYPT_SOME_ERR;
   void *buf = malloc(size);
   if (!buf)
      goto out;

   backend->buf = buf;
   backend->buf_size = size;
   rc = 0;

 out:
   return rc;
}

static void free_gpu_memory(struct gpu_backend *backend)
{
   memset(backend->buf, 0, backend->buf_size);
   free(backend->buf);
}

static int tell_gpu_alg(struct gpu_backend *backend, struct priv_cctx *cctx)
{
   int rc = -PRIV_CRYPT_SOME_ERR;
   struct priv_crypt_gpu_cmd_desc *cmd_desc =
      (struct priv_crypt_gpu_cmd_desc *)backend->buf;

   cmd_desc->cmd = PRIV_CRYPT_CMD_NEW_CTX;
   cmd_desc->desc.alg.alg = cctx->alg;
   cmd_desc->desc.alg.key_size = cctx->key_size;

   if (invoke_gpu(backend) < 0)
      goto out;

   /* XXX: Mark, do you want to include a return status from the GPU?
      If so, change rc here (you could make it the return value from
      invoke_gpu). */

   rc = 0;
 out:
   return rc;
}

/* We assume here the iv length is sensible, the main code will take
   care of that */
static int tell_gpu_iv(struct gpu_backend *backend, size_t len, uint8_t *iv)
{
   int rc = -PRIV_CRYPT_SOME_ERR;
   struct priv_crypt_gpu_cmd_desc *cmd_desc =
      (struct priv_crypt_gpu_cmd_desc *)backend->buf;

   cmd_desc->cmd = PRIV_CRYPT_CMD_SET_IV;
   cmd_desc->desc.iv.iv_size = (uint32_t)len;
   memcpy(cmd_desc->desc.iv.iv, iv, len);

   if (invoke_gpu(backend) < 0)
      goto out;

   /* XXX: Mark, return status? (see tell_gpu_alg) */

   rc = 0;
 out:
   return rc;
}

static int tell_gpu_kex_msg(struct gpu_backend *backend,
                            size_t *out_len, uint8_t *out_buf,
                            size_t in_len, uint8_t *in_buf)
{
   int rc = -PRIV_CRYPT_SOME_ERR;
   struct priv_crypt_gpu_cmd_desc *cmd_desc =
      (struct priv_crypt_gpu_cmd_desc *)backend->buf;
   struct priv_crypt_gpu_kex_desc *out_kex_desc =
      (struct priv_crypt_gpu_kex_desc *)(backend->buf + MAX_CMD_LEN);

   cmd_desc->cmd = PRIV_CRYPT_CMD_KEY_EXCHANGE;
   cmd_desc->desc.kex.size = in_len;
   memcpy(&cmd_desc->desc.kex.buf, in_buf, in_len);

   if (invoke_gpu(backend) < 0)
      goto out;

   if (out_kex_desc->size > MAX_CMD_LEN) {
      ERROR("%s: out size %lu > MAX_CMD_LEN\n",
            __func__, (unsigned long)out_kex_desc->size);
      goto out;
   }

   *out_len = out_kex_desc->size;
   memcpy(out_buf, &out_kex_desc->buf, out_kex_desc->size);

   if (*out_len > 0)
      rc = 1;
   else
      rc = 0;
 out:
   return rc;
}

static int tell_gpu_destroy(struct gpu_backend *backend)
{
   int rc = -PRIV_CRYPT_SOME_ERR;

   struct priv_crypt_gpu_cmd_desc *cmd_desc =
      (struct priv_crypt_gpu_cmd_desc *)backend->buf;

   cmd_desc->cmd = PRIV_CRYPT_CMD_DESTROY;
   /* no further fields for this command */

   if (invoke_gpu(backend) < 0)
      goto out;

   rc = 0;
 out:
   return rc;
}

static int gpu_init(void **result, struct priv_cctx *cctx)
{
   int rc = -PRIV_CRYPT_SOME_ERR;
   struct gpu_backend *backend;

   if (!result)
      goto out;

   backend = malloc(sizeof(*backend));
   if (!backend)
      goto out;

   if (alloc_gpu_memory(backend, 2*MAX_CMD_LEN) < 0)
      goto out_free;

   if (setup_gpu(backend) < 0)
      goto out_free2;

   if (tell_gpu_alg(backend, cctx) < 0)
      goto out_free2;

   *result = backend;
   rc = 0;
   goto out;

 out_free2:
   free(backend->buf);
 out_free:
   free(backend);
 out:
   return rc;
}

static int gpu_set_iv(void *backend_ctx, size_t len, uint8_t *iv)
{
   return tell_gpu_iv(backend_ctx, len, iv);
}

static int
gpu_send_kex_msg(void *backend_ctx,
                 size_t *out_size, uint8_t *out_buf,
                 size_t in_size, uint8_t *in_buf)
{
   return tell_gpu_kex_msg(backend_ctx,
                           out_size, out_buf,
                           in_size, in_buf);
}

static int
gpu_set_activation(void *backend_ctx, int activation)
{
   /* a no-op, gpu encryption always on once activated */
   return 0;
}

static void
gpu_destroy(void *backend_ctx)
{
   struct gpu_backend *backend = backend_ctx;

   tell_gpu_destroy(backend);
   free_gpu_memory(backend);
   free(backend);
}

struct priv_backend_ops gpu_backend_ops = {
   .init = gpu_init,
   .set_iv = gpu_set_iv,
   .send_kex_msg = gpu_send_kex_msg,
   .set_activation = gpu_set_activation,
   /* no .crypt */
   .destroy = gpu_destroy,
};
