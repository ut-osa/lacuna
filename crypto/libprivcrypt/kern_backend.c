#include <stdio.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <string.h>

#include <priv_crypt_kern.h>

#include <priv_crypt.h>
#include "priv_crypt_int.h"

#define DEBUG_KERN_BACKEND 1

#ifdef DEBUG_KERN_BACKEND
#define DPRINTF(fmt, args...) fprintf(stderr, fmt, ##args)
#else
#define DPRINTF(fmt, args...) do {} while(0)
#endif

struct kern_backend {
   int kern_fd;
   priv_crypt_id_t id;
};

static priv_crypt_id_t
priv_cmd_new_ctx(struct kern_backend *backend, struct priv_cctx *cctx)
{
   struct priv_crypt_alg_desc desc;

   desc.alg = cctx->alg;
   desc.key_size = cctx->key_size;
   desc.dir = cctx->dir;

   return (priv_crypt_id_t)ioctl(backend->kern_fd,
                                 PRIV_CRYPT_CMD_NEW_CTX,
                                 &desc);
}

static int kern_init(void **result, struct priv_cctx *cctx)
{
   int fd, rc = -PRIV_CRYPT_SOME_ERR;
   priv_crypt_id_t id;
   struct kern_backend *backend;
   const char crypt_ctl[] = "/dev/priv_crypt_ctl";

   if (!result)
      goto out;

   backend = malloc(sizeof(*backend));
   if (!backend)
      goto out;

   backend->kern_fd = open(crypt_ctl, O_RDWR);
   if (backend->kern_fd < 0) {
      DPRINTF("open priv_crypt_ctl failed\n");
      goto out_free;
   }

   if ((backend->id = priv_cmd_new_ctx(backend, cctx)) < 0) {
      DPRINTF("priv_cmd_new_ctx failed\n");
      goto out_close;
   }

   *result = backend;
   rc = 0;
   goto out;

 out_close:
   close(backend->kern_fd);
 out_free:
   free(backend);
 out:
   return rc;
}

static int
kern_set_iv(void *backend_ctx, size_t len, uint8_t *iv)
{
   int rc;

   struct kern_backend *backend = backend_ctx;
   struct priv_crypt_iv_desc desc;

   desc.iv_size = len;
   memcpy(desc.iv, iv, len);

   rc = ioctl(backend->kern_fd, PRIV_CRYPT_CMD_SET_IV, &desc);
   if (rc < 0) {
      DPRINTF("%s failed (rc=%d, errno=%d)\n",
              __func__, rc, errno);
      rc = -PRIV_CRYPT_SOME_ERR;
      goto out;
   }

   rc = 0;
 out:
   return rc;
}

static int
kern_send_kex_msg(void *backend_ctx,
                  size_t *out_size, uint8_t *out_buf,
                  size_t in_size, uint8_t *in_buf)
{
   int rc = -PRIV_CRYPT_SOME_ERR;
   struct priv_crypt_kex_msg_desc kex_msg_desc;
   struct kern_backend *backend = backend_ctx;

   DPRINTF("%s: kern_fd %d in_size %lu\n",
           __func__, backend->kern_fd, in_size);

   kex_msg_desc.in_size = in_size;
   kex_msg_desc.in_buf = in_buf;
   kex_msg_desc.out_buf = out_buf;

   if (ioctl(backend->kern_fd,
             PRIV_CRYPT_CMD_KEY_EXCHANGE,
             &kex_msg_desc) < 0)
      goto out;

   *out_size = kex_msg_desc.out_size;
   if (*out_size > 0)
      rc = 1;
   else
      rc = 0;

 out:
   return rc;
}

static int
priv_cmd_destroy_cctx(struct kern_backend *backend)
{
   return (int)ioctl(backend->kern_fd,
                     PRIV_CRYPT_CMD_DESTROY,
                     NULL);
}

static void
kern_destroy(void *backend_ctx)
{
   struct kern_backend *backend = backend_ctx;

   priv_cmd_destroy_cctx(backend);
   close(backend->kern_fd);

   free(backend_ctx);
}

static int
kern_crypt(void *backend_ctx, size_t len, uint8_t *data)
{
   int rc;
   struct priv_crypt_crypt_desc desc;
   struct kern_backend *backend = backend_ctx;

   desc.data_size = len;
   memcpy(desc.data, data, len);

   rc = ioctl(backend->kern_fd,
              PRIV_CRYPT_CMD_DEBUG_CRYPT,
              &desc);

   memcpy(data, desc.data, len);

   return rc;
}

static int
priv_cmd_activation(struct kern_backend *backend, int dir)
{
   int rc = -PRIV_CRYPT_SOME_ERR;

   if (dir) {
      if (ioctl(backend->kern_fd, PRIV_CRYPT_CMD_ACTIVATE, 0) < 0)
         goto out;
   } else {
      if (ioctl(backend->kern_fd, PRIV_CRYPT_CMD_DEACTIVATE, 0) < 0)
         goto out;      
   }

   rc = 0;
 out:
   return rc;
}

static int
kern_set_activation(void *backend_ctx, int activation)
{
   return priv_cmd_activation(backend_ctx, activation);
}

static int
kern_per_backend(void *backend_ctx, int req, void *req_data)
{
   if (!req_data)
      return -PRIV_CRYPT_SOME_ERR;

   *(priv_crypt_id_t *)req_data = ((struct kern_backend *)backend_ctx)->id;

   return 0;
}

struct priv_backend_ops kern_backend_ops = {
   .init = kern_init,
   .set_iv = kern_set_iv,
   .send_kex_msg = kern_send_kex_msg,
   .set_activation = kern_set_activation,
   .crypt = kern_crypt,
   .per_backend = kern_per_backend,
   .destroy = kern_destroy,
};
