#include <linux/kernel.h>
#include <linux/printk.h>
#include <linux/module.h>

#include <linux/crypto_math.h>
#include "priv_crypt_int.h"

#define KEX_DEBUG

#ifdef KEX_DEBUG
# define DPRINTF(fmt, args...) printk(KERN_DEBUG fmt, ##args)
# define DPRINT_HEX(len, buf) \
	print_hex_dump_bytes("", DUMP_PREFIX_OFFSET, buf, len)
#else
# define DPRINTF(...) do {} while(0)
# define DPRINT_HEX(len, buf) do {} while(0)
#endif

extern const struct dhm_kex_ops cm_dhm_kex_ops;

/*
 * Not assuming that input and output buffers don't overlap at the moment, but
 * will if it becomes necessary (some code leading in here will have to be
 * fixed)
 */
/*
 * out_sz (out) size of data put into it by process_kex_msg
 *        (in)  space available in out_buf
 */
int process_kex_msg(struct priv_crypt_ctx *cctx,
		    size_t *out_sz, u8 *out_buf,
		    size_t in_sz, u8 *in_buf)
{
	int rc = -1, i;
	u8 msg_type;
	u8 key_buf[MAX_KEY_LEN];
	struct dhm_kex *kex;
	const struct dhm_kex_ops *kex_ops;
#ifdef KEX_DEBUG
	const size_t debug_msg_sz = 256;
#endif

	if (in_sz < sizeof(u8)) {
		printk(KERN_ERR "kex_msg too short\n");
		rc = -1;
		goto out;
	}

	msg_type = in_buf[0];

	DPRINTF("%s: msg type %u\n", __func__, (unsigned int)msg_type);
	switch (msg_type) {
	case PRIV_CRYPT_KEX_PARAMS:
		kex_ops = symbol_request(cm_dhm_kex_ops);
		if (kex_ops == NULL) {
			printk(KERN_ERR "cannot load kex operations\n");
			goto put;
		}

		if ((rc = kex_ops->init(&kex)) < 0)
			goto put;

		*out_sz -= 1;
		if ((rc = kex_ops->respond(kex,
					   out_sz, out_buf+1,
					   in_sz-1, in_buf+1)) < 0)
			goto destroy;
		*out_sz += 1;

		if ((rc = kex_ops->get_key(kex, cctx->key_size, key_buf)) < 0)
			goto destroy;
		if ((rc = priv_set_key(cctx, cctx->key_size, key_buf)) < 0)
			goto destroy;

	destroy:
		kex_ops->destroy(kex);
		out_buf[0] = PRIV_CRYPT_KEX_RESPONSE;

	put:
		symbol_put(cm_dhm_kex_ops);

		if (rc < 0)
			goto out;

		break;
#ifdef KEX_DEBUG
	case PRIV_CRYPT_KEX_DEBUG:
		DPRINTF("message:\n");
		DPRINT_HEX(in_sz - sizeof(u8), in_buf + sizeof(u8));

		/* debug response message */
		out_buf[0] = PRIV_CRYPT_KEX_DEBUG;
		for (i = sizeof(u8); i < sizeof(u8) + debug_msg_sz; i++) {
			out_buf[i] = (u8)(i - 1);
		}
		*out_sz = sizeof(u8) + debug_msg_sz;
		break;
#endif
	default:
		printk(KERN_ERR "%s: unknown msg type %u\n",
		       __func__, msg_type);
		goto out;
		break;
	}

	rc = 0;

 out:
	memset(key_buf, 0, sizeof(key_buf));
	return rc;
}
