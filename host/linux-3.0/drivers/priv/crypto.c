#include <linux/priv_crypt_kern.h>
#include <linux/crypto.h>
#include <linux/err.h>
#include <linux/random.h>
#include <linux/string.h>
#include <asm-generic/errno-base.h>
#include <linux/scatterlist.h>
#include <linux/types.h>
#include <linux/mutex.h>

#include <linux/priv_crypt.h>
#include "priv_crypt_int.h"

#define REAL_ENCRYPT 1

static int setup_crypto_ctx(struct priv_crypt_ctx *cctx,
			    const char *alg, size_t key_size, size_t iv_size,
			    uint8_t dir)
{
	int rc = -1, i;
	u8 *page_mem;
	u8 temp_key[MAX_KEY_LEN];

	cctx->cipher = crypto_alloc_blkcipher(alg, 0, 0);
	if (IS_ERR(cctx->cipher)) {
		rc = PTR_ERR(cctx->cipher);
		goto out;
	}
	page_mem = (u8 *)__get_free_page(GFP_KERNEL);
	if (!page_mem) {
		rc = -ENOMEM;
		goto err;
	}
	cctx->crypto_pg_mem = page_mem;

	cctx->key_size = key_size;
	cctx->iv_size = iv_size;
	cctx->dir = (u8)dir;

        /* The following is a temporary key for testing purposes, otherwise we
	   eventually won't set a key here at all */
        for (i = 0; i < cctx->key_size; i++) {
		temp_key[i] = (u8)i;
	}
	if ((rc = priv_set_key(cctx, cctx->key_size, temp_key)) < 0)
		goto err;

	/* success */
	rc = 0;
	goto out;

 err:
	crypto_free_blkcipher(cctx->cipher);
 out:
	memset(temp_key, 0, sizeof(temp_key));
	return rc;
}

static priv_crypt_id_t get_fresh_id(void)
{
	static atomic_t id = ATOMIC_INIT(0);

	return (priv_crypt_id_t)atomic_add_return(1, &id);
}

/* Note: if we implement general block ciphers here, then we'll have
   to do some form of padding, see priv_encrypt. */
/* Also, if we change priv_crypt_id_t to not be compatible with int,
   then we'll have to modify how this function works (and how the
   ioctl works) */
int make_crypto_ctx(struct priv_crypt_ctx **result,
		    u32 alg, size_t key_size, int dir)
{
	int rc = -1;
	struct priv_crypt_ctx *cctx = NULL;

	if (!priv_crypt_alg_valid(alg)) {
		rc = -EINVAL;
		goto err;
	}
	if (dir != PRIV_CRYPT_DIR_ENC && dir != PRIV_CRYPT_DIR_DEC) {
		rc = -EINVAL;
		goto err;
	}

	cctx = kzalloc(sizeof(*cctx), GFP_KERNEL);
	if (!cctx) {
		rc = -ENOMEM;
		goto err;
	}

	switch (alg) {
	case PRIV_CRYPT_ALG_AES_CTR:
		if (key_size != 16 && key_size != 24 && key_size != 32) {
			rc = -EINVAL;
			goto err;
		}

		if ((rc = setup_crypto_ctx(cctx, "ctr(aes)",
					   key_size,
					   16,
					   (u8)dir)) < 0)
			goto err;

		break;
	default:
		goto err;
	}

	cctx->kex_buf = kmalloc(MAX_KEX_MSG_LEN, GFP_KERNEL);
	if (!cctx->kex_buf)
		goto err;

	cctx->id = get_fresh_id();
	mutex_init(&cctx->user_lock);

	cctx->user_ref = NULL;

	*result = cctx;

	rc = (int)cctx->id;
	goto out;

 err:
	if (cctx)
		kfree(cctx);
 out:
	return rc;
}

void destroy_crypto_ctx(struct priv_crypt_ctx *cctx)
{
	printk(KERN_DEBUG "destroy priv_crypt_ctx(%p)\n", cctx);

	/* XXX: double check that this zeroizes? */
	crypto_free_blkcipher(cctx->cipher);
	cctx->cipher = NULL;

	kfree(cctx->kex_buf);

	/* This page shouldn't be sensitive as long as our crypto
	   functions are never interrupted */
	__free_page(virt_to_page(cctx->crypto_pg_mem));

	memset(cctx->key, 0, sizeof(*cctx->key));
	/* iv is not sensitive */

	kfree(cctx);
}

int priv_set_key(struct priv_crypt_ctx *cctx, size_t key_size, u8 *key)
{
	int rc;

	if (key_size != cctx->key_size)
		return -EINVAL;

	memcpy(cctx->key, key, key_size);

#ifdef PRIV_CRYPT_DEBUG
	printk(KERN_DEBUG "setting key (%p):\n", cctx);
	print_hex_dump_bytes("", DUMP_PREFIX_OFFSET, cctx->key, cctx->key_size);
#endif

	rc = crypto_blkcipher_setkey(cctx->cipher, cctx->key, cctx->key_size);

	return rc;
}

int priv_set_iv(struct priv_crypt_ctx *cctx, size_t iv_size, u8 *iv)
{
	if (iv_size != cctx->iv_size)
		return -EINVAL;

	memcpy(cctx->iv, iv, iv_size);

#ifdef PRIV_CRYPT_DEBUG
	printk(KERN_DEBUG "setting iv (%p):\n", cctx);
	print_hex_dump_bytes("", DUMP_PREFIX_OFFSET, cctx->iv, cctx->iv_size);
#endif

	crypto_blkcipher_set_iv(cctx->cipher, cctx->iv, cctx->iv_size);

	return 0;
}

/* If debug mode is on, then allow encryption without
   activation check (but devices should still check this
   variable) */
#ifdef PRIV_CRYPT_DEBUG
static int allow_encrypt(struct priv_crypt_ctx *cctx)
{
	return 1;
}
#else
static int allow_encrypt(struct priv_crypt_ctx *cctx)
{
	return cctx->activated;
}
#endif

/* We'll use our own allocated page to ensure alignment and that
 * blocks do not stretch over pages.
 *
 * XXX: This code is not going to work with block ciphers - we'll have
 * to pad or otherwise do something.
 *
 * One possible scheme offhand: if 1 < block_size < 256 (bytes), then
 * if we fall short of a multiple of blocks, then add an extra byte
 * with the number of padding bytes in the last block.  We can
 * recognize padding from non-padding with divisibility by block size
 * and undo the padding easily.  (Appears not to be a security risk at
 * first glance either.)
 *
 * XXX: For large buffers, parts of this are inefficient (e.g. two
 * copies, many invocations of crypto fn)
 */
int priv_crypt(struct priv_crypt_ctx *cctx,
	       u8 *out_buf, u8 *in_buf, size_t len)
{
	u8 *crypto_pg_mem;
	struct page *crypto_pg;
	struct scatterlist sg;
	struct blkcipher_desc desc;
#ifdef PRIV_CRYPT_DEBUG
	u8 *orig_out_buf = out_buf;
	size_t orig_len = len;
#endif

	if (!cctx || !allow_encrypt(cctx))
		return -EINVAL;

#ifdef PRIV_CRYPT_DEBUG
	printk(KERN_EMERG "%s(%p,%p,%p,%lu)\n",
	       __func__, cctx, out_buf, in_buf, len);
	printk(KERN_DEBUG "first in bytes:\n");
	print_hex_dump_bytes("", DUMP_PREFIX_OFFSET, in_buf, MIN(len, 16));
#endif

	crypto_pg_mem = cctx->crypto_pg_mem;
	crypto_pg = virt_to_page(crypto_pg_mem);

	while (len > 0) {
		int amt, rc;

		amt = MIN(len, PAGE_SIZE);
		memcpy(crypto_pg_mem, in_buf, amt);

#if REAL_ENCRYPT
		sg_init_table(&sg, 1);
		sg_set_page(&sg, crypto_pg, amt, 0);

		desc.tfm = cctx->cipher;
		desc.flags = 0;

		if (cctx->dir == PRIV_CRYPT_DIR_ENC)
			rc = crypto_blkcipher_encrypt(&desc, &sg, &sg, amt);
		else
			rc = crypto_blkcipher_decrypt(&desc, &sg, &sg, amt);
		if (rc < 0) {
			printk(KERN_ERR "Encryption failure!\n");
			return rc;
		}
#else
		{
			int i;

			for (i = 0; i < amt; i++) {
				crypto_pg_mem[i] ^= 0xff;
			}
		}
#endif
		memcpy(out_buf, crypto_pg_mem, amt);

		in_buf += amt;
		out_buf += amt;
		len -= amt;
	}

#ifdef PRIV_CRYPT_DEBUG
	printk(KERN_DEBUG "first out bytes:\n");
	print_hex_dump_bytes("", DUMP_PREFIX_OFFSET, orig_out_buf, MIN(orig_len, 16));
#endif

	return 0;
}
EXPORT_SYMBOL_GPL(priv_crypt);

/* XXX: Locking? */
int priv_is_activated(struct priv_crypt_ctx *cctx)
{
	if (!cctx)
		return 0;

	return cctx->activated;
}
EXPORT_SYMBOL_GPL(priv_is_activated);

/* XXX: Locking? */
void priv_set_activated(struct priv_crypt_ctx *cctx, int activated)
{
	if (!cctx)
		return;

#ifdef PRIV_CRYPT_DEBUG
        printk(KERN_DEBUG "cctx %d %sactivated",
               cctx->id, (activated ? "": "de"));
#endif

	cctx->activated = activated;
}
