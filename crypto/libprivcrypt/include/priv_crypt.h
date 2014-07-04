#ifndef PRIV_CRYPT_H
#define PRIV_CRYPT_H

#include <stdlib.h>
#include <stdint.h>

struct priv_cctx;

typedef uint32_t priv_crypt_id_t;

/* Could abstract out the argument that this takes */
int priv_create_cctx(struct priv_cctx **result,
                     uint32_t alg, uint32_t key_size);
void priv_destroy_cctx(struct priv_cctx *cctx);

#define PRIV_OPTION_TEST_KEX 1
#define PRIV_OPTION_DIR_DEC 2

int priv_set_option(struct priv_cctx *cctx, uint32_t option);

int priv_new_backend(struct priv_cctx *cctx);
int priv_get_id(struct priv_cctx *cctx, priv_crypt_id_t *id);

/* asks the remote end to do the {en,de}cryption */
int priv_remote_crypt(struct priv_cctx *cctx, size_t len, uint8_t *data);

/* in place {en,de}cryption */
int priv_crypt(struct priv_cctx *cctx, size_t len, uint8_t *data);

/* Turn on or off the use of a cryptographic context by an */
int priv_activate(struct priv_cctx *cctx);
int priv_deactivate(struct priv_cctx *cctx);

/* XXX: The constants below are duplicated in a kernel header.  Is
   there a nicer way to do this that avoids some of the
   duplication? */

/* 0 -> no error */
#define PRIV_CRYPT_SOME_ERR 1
#define PRIV_CRYPT_NO_ALG 2

/* common constants throughout all backends */
#define MAX_KEY_LEN 64
#define MAX_IV_LEN 64

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

/* Kernel will only support PolarSSL DHM key exchange at the moment, the
   non-debug message numbers correspond to those messages */
/* params: P, G, G^x */
#define PRIV_CRYPT_KEX_PARAMS 1
/* response: G^y */
#define PRIV_CRYPT_KEX_RESPONSE 2
/* Has to be enabled with debug setting */
#define PRIV_CRYPT_KEX_DEBUG 255

#endif
