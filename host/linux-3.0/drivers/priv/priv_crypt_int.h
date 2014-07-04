#ifndef _PRIV_CRYPT_INT_H
#define _PRIV_CRYPT_INT_H

//#define PRIV_CRYPT_DEBUG

#include <linux/crypto.h>
#include <linux/mutex.h>
#include <linux/types.h>

#include <linux/priv_crypt_kern.h>

#define MIN(a, b) ((a) > (b) ? (b) : (a))

struct priv_crypt_ctx {
	priv_crypt_id_t id;
	struct list_head list;

	/* pid of allocating process */
	pid_t alloc_pid;

	/* for locking */
	struct mutex user_lock;
	struct priv_crypt_ctx_ref *user_ref;

	/* key exchange */
	u8 *kex_buf;

	/* encryption state */
	struct crypto_blkcipher *cipher;
	u8 *crypto_pg_mem;

	int activated;
	u8 dir;

	u8 key[MAX_KEY_LEN];
	size_t key_size;
	u8 iv[MAX_IV_LEN];
	size_t iv_size;
};

int make_crypto_ctx(struct priv_crypt_ctx **result,
		    u32 alg, size_t key_size, int dir);
void destroy_crypto_ctx(struct priv_crypt_ctx *cctx);

int process_kex_msg(struct priv_crypt_ctx *cctx,
		    size_t *out_sz, u8 *out_buf,
		    size_t in_sz, u8 *in_buf);

int priv_set_key(struct priv_crypt_ctx *cctx, size_t key_size, u8 *key);
int priv_set_iv(struct priv_crypt_ctx *cctx, size_t iv_size, u8 *iv);
void priv_set_activated(struct priv_crypt_ctx *cctx, int activated);

#endif
