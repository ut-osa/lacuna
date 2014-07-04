#include <linux/module.h>
#include <linux/random.h>
#include <linux/slab.h>

#include <linux/crypto_math.h>
#include "polarssl/bignum.h"
#include "polarssl/dhm.h"
#include "polarssl/ssl.h"

/* XXX: It is CRITICAL that you turn CRYPTO_MATH_DEBUG OFF for production (it
   prints the shared secret) */
//#define CRYPTO_MATH_DEBUG

extern int mpi_self_test(int verbose);

/* Test data taken from PolarSSL */
static const char dhm_test_P[] = \
"E4004C1F94182000103D883A448B3F802CE4B44A83301270002C20D0321CFD00" \
"11CCEF784C26A400F43DFB901BCA7538F2C6B176001CF5A0FD16D2C48B1D0C1C" \
"F6AC8E1DA6BCC3B4E1F96B0564965300FFA1D0B601EB2800F489AA512C4B248C" \
"01F76949A60BB7F00A40B1EAB64BDD48E8A700D60B7F1200FA8E77B0A979DABF";
static const char dhm_test_G[] = "4";

static int dhm_import_public(dhm_context *dhm_ctx, const char *P, const char *G)
{
	int rc = -1;

	if (mpi_read_string(&dhm_ctx->P, 16, P) != 0)
		goto out;
	if (mpi_read_string(&dhm_ctx->G, 16, G) != 0)
		goto out;

	rc = 0;

 out:
	if (rc < 0)
		printk(KERN_DEBUG "%s failed\n", __func__);

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

/* XXX: Check whether this RNG is reasonable and/or port in the
   PolarSSL one */
static int kernel_rng(void *state, unsigned char *out, size_t out_len)
{
	/* Actually ignore the state and just get random bytes from
	   the kernel */
	get_random_bytes(out, out_len);
	return 0;
}

static int make_params(dhm_context *ctx,
		       unsigned char *msg_buf, size_t *msg_len)
{
	int rc;

	if ((rc = dhm_make_params(ctx, mpi_size(&ctx->P),
				  msg_buf, msg_len,
				  kernel_rng, NULL)) != 0)
		return rc;

	/* It looks like there needs to be an extra suffix indicating
	   that 0 extra bytes are on the end (but the number needs to
	   be there...) */
	msg_buf[*msg_len] = 0;
	msg_buf[*msg_len+1] = 0;
	*msg_len += 2;

	return 0;
}

struct dhm_kex {
	dhm_context ctx;
};

static int cm_dhm_init(struct dhm_kex **kex_store)
{
	int rc = -1;
	struct dhm_kex *ctx;
	
	ctx = kzalloc(sizeof(*ctx), GFP_KERNEL);
	if (!ctx) {
		rc = -ENOMEM;
		goto out;
	}

	*kex_store = ctx;
	rc = 0;

 out:
	return rc;
}

static void cm_dhm_destroy(struct dhm_kex *kex)
{
	memset(kex, 0, sizeof(*kex));
	kfree(kex);
}

static int cm_dhm_respond(struct dhm_kex *kex,
		   size_t *out_buf_len, u8 *out_buf,
		   size_t in_buf_len, u8 *in_buf)
{
	int rc = -1;
	dhm_context *ctx = &kex->ctx;
	unsigned char *tmp;

	tmp = in_buf;
	if ((rc = dhm_read_params(ctx, &tmp, tmp + in_buf_len)) != 0) {
		printk(KERN_ERR "dhm_read_params: rc = %d\n", rc);
		rc = -1;
		goto out;
        }

	/* max_dhm_buf_size is overestimating here, but this shouldn't be an
	   issue */
	if (*out_buf_len < ctx->len) {
		rc = -ENOMEM;
		goto out;
	}

        if ((rc = dhm_make_public(ctx, mpi_size(&ctx->P),
                                  out_buf, ctx->len,
                                  kernel_rng, NULL)) != 0) {
		printk(KERN_ERR "dhm_make_public: rc = %d\n", rc);
		rc = -1;
		goto out;
	}
	if ((rc = dhm_calc_secret_no_export(ctx)) < 0) {
		printk(KERN_ERR "dhm_calc_secret_no_export: rc = %d\n", rc);
		rc = -1;
		goto out;
	}

#ifdef CRYPTO_MATH_DEBUG
	{
		size_t slen = mpi_size(&ctx->K);
		unsigned char *tmp_buf;
		
		tmp_buf = kmalloc(slen, GFP_KERNEL);
		if (!tmp_buf) {
			printk(KERN_ERR "couldn't get memory for "
			       "writing out shared pre-secret?\n");
		} else {
			mpi_write_binary(&ctx->K, tmp_buf, slen);
			printk(KERN_DEBUG "shared pre-secret:\n");
			print_hex_dump_bytes("", DUMP_PREFIX_OFFSET,
					     tmp_buf, slen);
			kfree(tmp_buf);
		}
	}
#endif

	*out_buf_len = ctx->len;
	rc = 0;

 out:
	return rc;
}

/* Use (something pretty close to) TLS v1.1 to calculate the
   secret  */
/* XXX: This function is copied at the moment from the userspace version... */
static int calc_secret(size_t key_len, u8 *key,
                       dhm_context *ctx)
{
	int i, rc = -1;
	unsigned char master[48];
	unsigned char random[64], swp[32];
	size_t pre_master_len;
	unsigned char *pre_master;

	pre_master_len = mpi_size(&ctx->K);
	pre_master = kmalloc(pre_master_len, GFP_KERNEL);
	if (!pre_master)
		goto out;

	if ((rc = mpi_write_binary(&ctx->K, pre_master, pre_master_len)) < 0)
		goto out_free;

	/* XXX: Do we really need extra server and client randomness here,
	   especially since the server and client trust each other? */
	for (i = 0; i < 64; i++) {
		random[i] = (unsigned char)i;
	}

	if ((rc = tls1_prf(pre_master, pre_master_len,
			   "master secret",
			   random, 64,
			   master, 48)) < 0)
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

#ifdef CRYPTO_MATH_DEBUG
	printk(KERN_DEBUG "shared key:\n");
	print_hex_dump_bytes("", DUMP_PREFIX_OFFSET, key, key_len);
#endif

	/* done! */
	rc = 0;

 out_free:
	memset(pre_master, 0, pre_master_len);
	kfree(pre_master);
 out:
	memset(master, 0, sizeof(master));
	/* clearing random not really needed now */
	memset(random, 0, sizeof(random));
	return rc;
}

static int cm_dhm_get_key(struct dhm_kex *kex, size_t key_len, u8 *key)
{
	int rc = -1;

	if ((rc = calc_secret(key_len, key, &kex->ctx)) < 0) {
		printk(KERN_ERR "%s: calc_secret (%d)\n",
		       __func__, rc);
		rc = -1;
		goto out;
	}

	rc = 0;

 out:
	return rc;
}

/* This is just a way of making it easier to export all the functions at once */
const struct dhm_kex_ops cm_dhm_kex_ops = {
	.init = cm_dhm_init,
	.destroy = cm_dhm_destroy,
	.respond = cm_dhm_respond,
	.get_key = cm_dhm_get_key,
};
/* necessary for symbol_request */
EXPORT_SYMBOL_GPL(cm_dhm_kex_ops);

static int dhm_test(int verbose)
{
	int rc = -1;
	dhm_context dhm_ctx_party1, dhm_ctx_party2;
	unsigned char *msg_buf = NULL, *tmp;
	size_t msg_buf_sz, msg_sz;

	memset(&dhm_ctx_party1, 0, sizeof(dhm_context));
	memset(&dhm_ctx_party2, 0, sizeof(dhm_context));

	/* This will likely be done by QEMU in our system */
	if ((rc = dhm_import_public(&dhm_ctx_party1, dhm_test_P, dhm_test_G)) < 0)
		goto out;

	msg_buf_sz = max_dhm_buf_size(&dhm_ctx_party1);
	msg_buf = kmalloc(msg_buf_sz, GFP_KERNEL);
	if (!msg_buf) {
		rc = -ENOMEM;
		goto out;
	}

	if ((rc = make_params(&dhm_ctx_party1, msg_buf, &msg_sz)) != 0) {
		printk(KERN_DEBUG "dhm_make_params failed: %x\n", rc);
		rc = -1;
		goto out;
	}

	/* This is the part that the kernel will actually end up doing */
	tmp = msg_buf;
	if ((rc = dhm_read_params(&dhm_ctx_party2, &tmp, tmp + msg_sz)) != 0) {
		printk(KERN_DEBUG "dhm_read_params failed: %x\n", rc);
		rc = -1;
		goto out;
	}

	/* Reusing the same message buffer - guaranteed to be shorter */
	if ((rc = dhm_make_public(&dhm_ctx_party2, mpi_size(&dhm_ctx_party2.P),
				  msg_buf, dhm_ctx_party2.len,
				  kernel_rng, NULL)) != 0) {
		printk(KERN_DEBUG "dhm_make_public failed: %x\n", rc);
		rc = -1;
		goto out;
	}

	/* Again, next part likely done by QEMU */
	if ((rc = dhm_read_public(&dhm_ctx_party1, msg_buf, dhm_ctx_party2.len)) != 0) {
		printk(KERN_DEBUG "dhm_read_public failed: %x\n", rc);
		rc = -1;
		goto out;
	}

	/* Calculate the secret from the exchange */
	if ((rc = dhm_calc_secret(&dhm_ctx_party1, msg_buf, &msg_sz)) != 0) {
		printk(KERN_DEBUG "dhm_calc_secret failed: %x\n", rc);
		rc = -1;
		goto out;
	}

	if ((rc = dhm_calc_secret(&dhm_ctx_party2, msg_buf, &msg_sz)) != 0) {
		printk(KERN_DEBUG "dhm_calc_secret failed: %x\n", rc);
		rc = -1;
		goto out;
	}

	/* Compare results */
	if ((rc = mpi_cmp_abs(&dhm_ctx_party1.K, &dhm_ctx_party2.K)) != 0) {
		printk(KERN_DEBUG "dhm final comparison failed: %x\n", rc);
		rc = -1;
		goto out;
	}

	/* Since this is just a test case, printing the shared secret
	   is not a problem */
	if (verbose) {
		size_t slen = msg_buf_sz;

		/* Again, counting on msg_buf to be the proper length,
		   which it seems it should (at least 2*len(P) + 1) */
		printk(KERN_DEBUG "dhm test success, shared pre-master secret:\n");
		mpi_write_string(&dhm_ctx_party1.K, 16, msg_buf, &slen);
		printk(KERN_DEBUG "%s\n", msg_buf);
	}

	rc = 0;

 out:
	if (msg_buf)
		kfree(msg_buf);

	return rc;
}

static int run_tests(int verbose)
{
	if (mpi_self_test(verbose) != 0) {
		printk(KERN_DEBUG "mpi_self_test failed\n");
		return -1;
	}

	if (dhm_test(verbose) != 0) {
		printk(KERN_DEBUG "dhm_test failed\n");
		return -1;
	}

	printk(KERN_DEBUG "crypto_math tests succeeded\n");

	return 0;
}

int crypto_math_init(void)
{
	int rc, verbose = 1;

	if ((rc = run_tests(verbose)) < 0)
		return rc;

	return 0;
}

void crypto_math_exit(void)
{
}

module_init(crypto_math_init);
module_exit(crypto_math_exit);

MODULE_LICENSE("GPL");
