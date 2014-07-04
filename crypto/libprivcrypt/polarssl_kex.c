#include <priv_crypt.h>
#include <priv_crypt_kern.h>
#include "priv_crypt_int.h"

#include <polarssl/bignum.h>
#include <polarssl/dhm.h>

#define KEX_START 0
#define KEX_INIT_SENT 1
#define KEX_DONE 2

/* XXX: It is CRITICAL that you turn DEBUG_POLARSSL_KEX OFF for
   production (it prints the shared secret) */
//#define DEBUG_POLARSSL_KEX

#define ERROR(fmt, args...) fprintf(stderr, fmt, ##args)
#ifdef DEBUG_POLARSSL_KEX
#define DPRINTF(fmt, args...) fprintf(stderr, fmt, ##args)
#else
#define DPRINTF(fmt, args...) do {} while(0)
#endif

struct polarssl_kex_ctx {
   size_t key_size;
   u8 *key;
   dhm_context dhm_ctx;
   int state;
};

static int polarssl_init(void **result, size_t key_size)
{
   int rc = -PRIV_CRYPT_SOME_ERR;
   struct polarssl_kex_ctx *ctx;

   *result = NULL;

   ctx = malloc(sizeof(*ctx));
   if (!ctx)
      goto out;

   ctx->key_size = key_size;
   ctx->key = malloc(key_size);
   if (!ctx->key)
      goto out_free;

   ctx->state = KEX_START;
   memset(&ctx->dhm_ctx, 0, sizeof(ctx->dhm_ctx));

   *result = ctx;
   rc = 0;
   goto out;

 out_free:
   free(ctx);
 out:
   return rc;
}

static int polarssl_get_key(void *kex_ctx, uint8_t *key_buf)
{
   struct polarssl_kex_ctx *ctx = kex_ctx;

   memcpy(key_buf, ctx->key, ctx->key_size);

   return 0;
}

/* Data taken from PolarSSL */
static const char dhm_sample_P[] = \
"E4004C1F94182000103D883A448B3F802CE4B44A83301270002C20D0321CFD00" \
"11CCEF784C26A400F43DFB901BCA7538F2C6B176001CF5A0FD16D2C48B1D0C1C" \
"F6AC8E1DA6BCC3B4E1F96B0564965300FFA1D0B601EB2800F489AA512C4B248C" \
"01F76949A60BB7F00A40B1EAB64BDD48E8A700D60B7F1200FA8E77B0A979DABF";
static const char dhm_sample_G[] = "4";

static int get_public(dhm_context *dhm_ctx)
{
   /* XXX: For the moment, just use the same P and G every time.  This
      should be secure, but perhaps we want to do something
      different? */
   int rc = -1;

   if (mpi_read_string(&dhm_ctx->P, 16, dhm_sample_P) != 0)
      goto out;
   if (mpi_read_string(&dhm_ctx->G, 16, dhm_sample_G) != 0)
      goto out;

   rc = 0;

 out:
   return rc;
}

static size_t max_dhm_buf_size(dhm_context *ctx)
{
   /* The logic here is that P, being the modulus, is as large as
      any of the possible (3) numbers P, G, GX that go in the
      initial message.  The size of the numbers are also put
      before them as 2 byte quantities.  Also, 2 bytes extra at
      the end. */
   /* This will also be the largest message */
   return 3 * (mpi_size(&ctx->P) + 2) + 2;
}

static int null_rng(void *state, unsigned char *out, size_t out_len)
{
   /* Can't output 0 always, causes some trouble with PolarSSL */
   ERROR("Using a nonsense rng (outputs 0x01 bytes always)\n");

   memset(out, 1, out_len);
   return 0;
}

static int make_params(dhm_context *ctx,
		       unsigned char *msg_buf, size_t *msg_len,
                       int (*rng)(void *state, unsigned char *out, size_t out_len))
{
   int rc;

   if ((rc = dhm_make_params(ctx, mpi_size(&ctx->P),
                             msg_buf, msg_len,
                             rng, NULL)) != 0)
      return rc;

   /* It looks like there needs to be an extra suffix indicating
      that 0 extra bytes are on the end (but the number needs to
      be there...) */
   msg_buf[*msg_len] = 0;
   msg_buf[*msg_len+1] = 0;
   *msg_len += 2;

   return 0;
}

/* Copy the PolarSSL function, but change it to not export the secret
   itself */
/*
 * Derive and export the shared secret (G^Y)^X mod P
 */
static int calc_pre_secret(dhm_context *ctx)
{
    int ret;

    if( ctx == NULL )
        return( POLARSSL_ERR_DHM_BAD_INPUT_DATA );

    MPI_CHK( mpi_exp_mod( &ctx->K, &ctx->GY, &ctx->X,
                          &ctx->P, &ctx->RP ) );

    if( ( ret = dhm_check_range( &ctx->GY, &ctx->P ) ) != 0 )
        return( ret );

cleanup:

    if( ret != 0 )
        return( POLARSSL_ERR_DHM_CALC_SECRET_FAILED + ret );

    return( 0 );
}

/* Use (something pretty close to) TLS v1.1 to calculate the
   secret  */
static int calc_secret(size_t key_len, uint8_t *key,
                       dhm_context *ctx)
{
   int i, rc = -1;
   unsigned char master[48];
   unsigned char random[64], swp[32];
   size_t pre_master_len;
   unsigned char *pre_master;

   pre_master_len = mpi_size(&ctx->K);
   pre_master = malloc(pre_master_len);
   if (!pre_master)
      goto out;

   if ((rc = mpi_write_binary(&ctx->K, pre_master, pre_master_len)) < 0)
      goto out_free;

   /* XXX: Do we really need extra server and client randomness here,
      especially since the server and client trust each other? */
   for (i = 0; i < 64; i++) {
      random[i] = (unsigned char)i;
   }

   /* AD: looks like (by viewing the assembly) without the explicit
      cast that the last argument is not sized properly and leads to a
      bogus value being sent to the function */
   if ((rc = tls1_prf(pre_master, pre_master_len,
                      "master secret",
                      random, (size_t)64,
                      master, (size_t)48)) < 0)
      goto out_free;

   /* swap the "random" piece */
   memcpy(swp, random+32, 32);
   memcpy(random+32, random, 32);
   memcpy(random, swp, 32);

   if ((rc = tls1_prf(master, sizeof(master), 
                      "key expansion",
                      random, 64,
                      key, key_len)) < 0)
      goto out_free;

   /* done! */
   rc = 0;

 out_free:
   memset(pre_master, 0, pre_master_len);
   free(pre_master);
 out:
   memset(master, 0, sizeof(master));
   /* clearing random not really needed now */
   memset(random, 0, sizeof(random));
   return rc;
}

#ifdef DEBUG_POLARSSL_KEX
void print_pre_secret(dhm_context *ctx)
{
   char *buf;
   size_t slen = mpi_size(&ctx->K);

   buf = malloc(slen);
   if (!buf) {
      ERROR("%s: out of memory\n", __func__);
   } else {
      mpi_write_binary(&ctx->K, buf, slen);
      DPRINTF("shared pre-secret:\n");
      print_hex(slen, buf);
      free(buf);
   }
}

void print_key(size_t key_len, uint8_t *key)
{
   DPRINTF("shared key:\n");
   print_hex(key_len, key);
}
#endif

static int polarssl_process_msg(void *kex_ctx,
                                size_t *out_len, uint8_t *out_buf,
                                size_t in_len, uint8_t *in_buf)
{
   int rc = -PRIV_CRYPT_SOME_ERR;
   struct polarssl_kex_ctx *ctx = kex_ctx;
   dhm_context *dhm_ctx = &ctx->dhm_ctx;

   DPRINTF("%s: state %d\n", __func__, ctx->state);

   switch (ctx->state) {
   case KEX_START:
      if ((rc = get_public(dhm_ctx)) < 0)
         goto out;
      if (max_dhm_buf_size(dhm_ctx) > MAX_KEX_MSG_LEN - sizeof(u8)) {
         ERROR("%s: dhm message too large for buffer\n",
               __func__);
         rc = -PRIV_CRYPT_SOME_ERR;
         goto out;
      }

      out_buf[0] = PRIV_CRYPT_KEX_PARAMS;
      /* XXX: Fix rng! */
      if ((rc = make_params(dhm_ctx, out_buf+1, out_len, null_rng)) < 0)
         goto out;
      *out_len += 1;

      ctx->state = KEX_INIT_SENT;
      rc = 1;
      break;
   case KEX_INIT_SENT:
      if (in_len < sizeof(u8)) {
         ERROR("%s: buffer too short?\n",
               __func__);
         goto out;
      }
      if (in_buf[0] != PRIV_CRYPT_KEX_RESPONSE) {
         ERROR("%s: wrong type of packet (%d)?\n",
               __func__, in_buf[0]);
         goto out;
      }
      if ((rc = dhm_read_public(dhm_ctx, in_buf + 1, in_len - 1)) < 0) {
         ERROR("%s: get_other_public (%d)\n",
               __func__, rc);
         rc = -1;
         goto out;
      }
      if ((rc = calc_pre_secret(dhm_ctx)) < 0) {
         ERROR("%s: calc_pre_secret (%d)\n",
               __func__, rc);
         rc = -1;
         goto out;
      }
#ifdef DEBUG_POLARSSL_KEX
      print_pre_secret(dhm_ctx);
#endif
      if ((rc = calc_secret(ctx->key_size, ctx->key,
                            dhm_ctx)) < 0) {
         ERROR("%s: calc_secret (%d)\n",
               __func__, rc);
         rc = -1;
         goto out;
      }
#ifdef DEBUG_POLARSSL_KEX
      print_key(ctx->key_size, ctx->key);
#endif

      /* XXX: do final secret calculation here */

      ctx->state = KEX_DONE;
      rc = 0;
      break;
   case KEX_DONE:
      rc = 0;
      break;
   default:
      error("%s: unexpected state\n",
            __func__);
      break;
   }

 out:
   return rc;
}

static int polarssl_destroy(void *kex_ctx)
{
   struct polarssl_kex_ctx *ctx = kex_ctx;

   memset(ctx->key, 0, ctx->key_size);
   free(ctx->key);
   memset(ctx, 0, sizeof(*ctx));
   free(ctx);
   return 0;
}

struct priv_kex_ops polarssl_kex_ops = {
   .init = polarssl_init,
   .get_key = polarssl_get_key,
   .process_msg = polarssl_process_msg,
   .destroy = polarssl_destroy,
};
