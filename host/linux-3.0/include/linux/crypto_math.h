#ifndef _LINUX_CRYPTO_MATH_H
#define _LINUX_CRYPTO_MATH_H

/* This could be generalized to more types of key exchange without too much
   difficulty, but we're not going to for now */
struct dhm_kex;
struct dhm_kex_ops {
	int (*init)(struct dhm_kex **kex_store);
	void (*destroy)(struct dhm_kex *kex);
	int (*respond)(struct dhm_kex *kex,
		       size_t *out_buf_len, u8 *out_buf,
		       size_t in_buf_len, u8 *in_buf);
	int (*get_key)(struct dhm_kex *kex, size_t key_len, u8 *key);
};

#endif
