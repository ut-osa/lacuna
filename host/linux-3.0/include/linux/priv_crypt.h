#ifndef PRIV_CRYPT_H
#define PRIV_CRYPT_H

#include <linux/types.h>

#include <linux/priv_crypt_kern.h>

struct priv_crypt_ctx;

/* access to cryptographic contexts:
 *
 * use_ctx: give a place for a ctx ptr, the place can be set to NULL
 * if the ctx is destroyed
 *
 * unuse_ctx: inform the crypto subsystem that the storage place is
 * going away
 */
struct priv_crypt_ctx_ref {
	spinlock_t lock;
	struct priv_crypt_ctx *cctx;
};

void cctx_ref_init(struct priv_crypt_ctx_ref *cctx_ref);

int use_ctx(priv_crypt_id_t id, struct priv_crypt_ctx_ref *cctx_ref);
void unuse_ctx(struct priv_crypt_ctx_ref *cctx_ref);

/* does encryption or decryption depending on how ctx was set up */
int priv_crypt(struct priv_crypt_ctx *cctx,
	       u8 *out_buf, u8 *in_buf, size_t len);
int priv_is_activated(struct priv_crypt_ctx *cctx);

void priv_pid_death_action(pid_t pid);

struct device;
struct usb_hcd;
struct urb;
/* USB-related functionality */
int priv_usb_work_setup(struct device *dev);
void priv_usb_work_destroy(struct device *dev);
void priv_schedule_giveback_urb(struct usb_hcd *hcd,
				struct urb *urb,
				int status);

int device_setup_cctx_attribute(struct device *dev);
void device_destroy_cctx_attribute(struct device *dev);

#endif
