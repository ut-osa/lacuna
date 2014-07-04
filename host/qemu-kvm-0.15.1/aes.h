#ifndef QEMU_AES_H
#define QEMU_AES_H

typedef union {
    uint64_t ll[2];
    uint8_t b[16];
} AES_IV;

#define SET_IV(iv, l1, l2) { (iv).ll[0] = (l1); (iv).ll[1] = (l2); }

#ifdef CONFIG_QCOW_AESNI

/* aes_ctx and aesni_state from aesni.c in libprivcrypt.  libprivcrypt
 * is meant for ephemeral channels in which data are stream-like
 * (without offset), which does not fit well with image encryption.
 * Image encryption will use its internal aesni routines and data
 * structures for AES CBC encryption. */
struct aes_ctx {
    uint8_t key_enc[240];
    uint8_t key_dec[240];
    uint32_t key_len;
};

struct aesni_state {
    /* changed from pointer to 16byte-aligned embedded structure */
    struct aes_ctx aesni_ctx;
    uint8_t iv[16];
};

extern int aesni_set_key(struct aes_ctx *ctx, const uint8_t *in_key,
                         unsigned int key_len);

extern void aesni_cbc_enc(struct aes_ctx *ctx, unsigned char *out,
                          const unsigned char *in, unsigned int len, uint8_t *iv);
extern void aesni_cbc_dec(struct aes_ctx *ctx, unsigned char *out,
                          const unsigned char *in, unsigned int len, uint8_t *iv);

typedef struct aes_ctx AES_KEY;

#else /* !CONFIG_QCOW_AESNI */
#define AES_MAXNR 14
#define AES_BLOCK_SIZE 16

struct aes_key_st {
    uint32_t rd_key[4 *(AES_MAXNR + 1)];
    int rounds;
};
typedef struct aes_key_st AES_KEY;

int AES_set_encrypt_key(const unsigned char *userKey, const int bits,
	AES_KEY *key);
int AES_set_decrypt_key(const unsigned char *userKey, const int bits,
	AES_KEY *key);

void AES_encrypt(const unsigned char *in, unsigned char *out,
	const AES_KEY *key);
void AES_decrypt(const unsigned char *in, unsigned char *out,
	const AES_KEY *key);
#endif /* CONFIG_QCOW_AESNI */
void AES_cbc_encrypt(const unsigned char *in, unsigned char *out,
		     const unsigned long length, const AES_KEY *key,
		     unsigned char *ivec, const int enc);

#endif
