#include <linux/usb.h>
#include <linux/usb/hcd.h>
#include <linux/workqueue.h>
#include <linux/slab.h>

#include <linux/priv_crypt.h>

struct urb_encrypt_work {
	struct usb_hcd *hcd;
	struct urb *urb;
	int status;
	
	struct work_struct work;
};

int priv_usb_work_setup(struct device *dev)
{
	int rc = -ENOMEM;

	dev->cctx_priv = kmalloc(sizeof(struct urb_encrypt_work), GFP_KERNEL);
	if (!dev->cctx_priv)
		goto out;

	rc = 0;

 out:
	return rc;
}
EXPORT_SYMBOL_GPL(priv_usb_work_setup);

void priv_usb_work_destroy(struct device *dev)
{
	kfree(dev->cctx_priv);
}
EXPORT_SYMBOL_GPL(priv_usb_work_destroy);

static int priv_usb_urb_enc(struct urb *urb)
{
	int rc = -1;
	struct priv_crypt_ctx *cctx;
	struct priv_crypt_ctx_ref *cctx_ref = NULL;
	struct device *dev;
	unsigned long flags;

	if (!urb->ep->ep_dev)
		goto out;

	dev = ep_device_to_device(urb->ep->ep_dev);
	cctx_ref = &dev->cctx_ref;

	spin_lock_irqsave(&cctx_ref->lock, flags);
	if (cctx_ref->cctx) {
		cctx = cctx_ref->cctx;

#if 0
		printk(KERN_EMERG "%s says hi (ep_%02x, flags %x)\n",
		       __func__,
		       urb->ep->desc.bEndpointAddress,
		       urb->transfer_flags);
#endif

		/* Not expecting this case... */
		BUG_ON(!urb->transfer_buffer);

		if (!priv_is_activated(cctx)) {
			printk(KERN_ERR "%s: context no longer activated"
			       " - erasing data\n", __func__);
			goto out_erase;
		}

		if (priv_crypt(cctx,
			       urb->transfer_buffer,
			       urb->transfer_buffer,
			       urb->actual_length) < 0) {
			printk(KERN_ERR "priv device encryption error"
			       " - erasing data\n");
			goto out_erase;
		}
	}

	rc = 0;
	goto out_put_ref;

 out_erase:
	/* make it seem like a (recoverable) error
	 *
	 * XXX: does this work? is there a way to just resubmit the URB?
	 */
	rc = -EPIPE;
	memset(urb->transfer_buffer, 0, urb->actual_length);
 out_put_ref:
	spin_unlock_irqrestore(&cctx_ref->lock, flags);
 out:
	return rc;
}

static void encrypt_and_giveback_urb(struct work_struct *work)
{
	int rc;

	struct urb_encrypt_work *urb_work =
		container_of(work, struct urb_encrypt_work, work);

	rc = priv_usb_urb_enc(urb_work->urb);
	usb_hcd_giveback_urb(urb_work->hcd,
			     urb_work->urb,
			     rc < 0 ? rc : urb_work->status);
}

/* Schedule giveback of URB, with encryption if appropriate */
void priv_schedule_giveback_urb(struct usb_hcd *hcd,
				struct urb *urb,
				int status)
{
	int just_giveback = 0;
	struct priv_crypt_ctx_ref *cctx_ref = NULL;
	struct device *dev;
	unsigned long flags;

	if (urb->ep->ep_dev) {
		dev = ep_device_to_device(urb->ep->ep_dev);
		cctx_ref = &dev->cctx_ref;
	}

	if (cctx_ref && status >= 0) {
		/* schedule encryption */
		struct urb_encrypt_work *urb_work =
			(struct urb_encrypt_work *)dev->cctx_priv;

		spin_lock_irqsave(&cctx_ref->lock, flags);
		if (cctx_ref->cctx && priv_is_activated(cctx_ref->cctx)) {
			urb_work->hcd = hcd;
			urb_work->urb = urb;
			urb_work->status = status;

			INIT_WORK(&urb_work->work, encrypt_and_giveback_urb);

			/* XXX: Better action to take here? */
			if (schedule_work(&urb_work->work) < 0) {
				printk(KERN_ERR "%s: couldn't schedule work!\n",
				       __func__);
			}
		} else {
			just_giveback = 1;
		}
		spin_unlock_irqrestore(&cctx_ref->lock, flags);
	} else {
		/* just give URB back (status < 0 case -> error in URB handling
		   anyway -> no data should be in the URB) */
		just_giveback = 1;
	}

	if (just_giveback == 1)
		usb_hcd_giveback_urb(hcd, urb, status);
}
EXPORT_SYMBOL_GPL(priv_schedule_giveback_urb);
