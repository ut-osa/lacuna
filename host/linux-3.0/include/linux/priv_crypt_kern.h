#ifndef PRIV_CRYPT_KERN_H
#define PRIV_CRYPT_KERN_H

/* Common user/kernel data structures and constants */

#ifndef __KERNEL__
# include <stdint.h>
# define u32 uint32_t
# define u8 uint8_t
#else
# include <linux/types.h>

typedef u32 priv_crypt_id_t;
#endif

#define MAX_KEY_LEN 64
#define MAX_IV_LEN 64

#define MAX_CRYPT_LEN 4096
#define MAX_KEX_MSG_LEN 4096

#define PRIV_CRYPT_ALG_AES_CTR 1

/* Commands and data structures */
#define PRIV_CRYPT_CMD_NEW_CTX 1
#define PRIV_CRYPT_CMD_USE_CTX 2
#define PRIV_CRYPT_CMD_SET_IV 3
#define PRIV_CRYPT_CMD_ACTIVATE 4
#define PRIV_CRYPT_CMD_DEACTIVATE 5
#define PRIV_CRYPT_CMD_DESTROY 6
#define PRIV_CRYPT_CMD_KEY_EXCHANGE 7
/* Has to be specifically allowed in the kernel with debug setting */
#define PRIV_CRYPT_CMD_DEBUG_CRYPT 31

#define PRIV_CRYPT_DIR_ENC 1
#define PRIV_CRYPT_DIR_DEC 2

/* Kernel will only support PolarSSL DHM key exchange at the moment, the
   non-debug message numbers correspond to those messages */
/* params: P, G, G^x */
#define PRIV_CRYPT_KEX_PARAMS 1
/* response: G^y */
#define PRIV_CRYPT_KEX_RESPONSE 2
/* Has to be enabled with debug setting */
#define PRIV_CRYPT_KEX_DEBUG 255

struct priv_crypt_alg_desc {
	u32 alg;
	u32 key_size;
	u8 dir;
} __attribute__((packed));

struct priv_crypt_iv_desc {
	u32 iv_size;
	u8 iv[MAX_IV_LEN];
} __attribute__((packed));

struct priv_crypt_crypt_desc {
	u32 data_size;
	u8 data[MAX_CRYPT_LEN];
} __attribute__((packed));

struct priv_crypt_kex_msg_desc {
	u32 in_size;
	u8 *in_buf;
	u32 out_size;
	u8 *out_buf;
} __attribute__((packed));

static inline int priv_crypt_alg_valid(u32 alg)
{
	return (alg == PRIV_CRYPT_ALG_AES_CTR);
}

#endif
