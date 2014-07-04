#include <priv_crypt.h>
#include "priv-util.h"

#include <stdio.h>
#include <string.h>

int priv_easy_create(struct priv_cctx **cctx_store, int dir_in)
{
   int rc = -1;
   struct priv_cctx *cctx;

   rc = priv_create_cctx(&cctx, PRIV_CRYPT_ALG_AES_CTR, 16);
   if (rc < 0)
      goto out;

   if (!dir_in) {
      rc = priv_set_option(cctx, PRIV_OPTION_DIR_DEC);
      if (rc < 0)
         goto out_destroy;
   }

   rc = priv_new_backend(cctx);
   if (rc < 0)
      goto out_destroy;

   *cctx_store = cctx;
   rc = 0;
   goto out;
 out_destroy:
   priv_destroy_cctx(cctx);
 out:
   return rc;
}

void print_hex(uint8_t *buf, size_t len)
{
    int i;
    for (i = 0; i < len; i++) {
        if (i > 0 && !(i % 16))
            printf("\n");
        printf("%02x ", *buf++);
    }
    printf("\n");
}

void sstrncpy(char *dest, const char *src, size_t n)
{
    strncpy(dest, src, n);
    if (n > 0)
        dest[n - 1] = (char)0;
}

/*
 * Local variables:
 * indent-tabs-mode: nil
 * c-basic-offset: 4
 * End:
 */
