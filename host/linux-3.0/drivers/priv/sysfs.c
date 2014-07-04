#include <linux/device.h>
#include <linux/slab.h>

#include <linux/priv_crypt.h>

/* Convenience functions for tying an encryption context to a device.  Note that
   you do not have to have a device-level object for using an encryption
   context. */

static ssize_t device_cctx_id_show(struct device *dev,
				   struct device_attribute *attr,
				   char *buf)
{
	ssize_t sz;

	sz = snprintf(buf, 16, "%d", (int)dev->cctx_id);
	return sz;
}

static ssize_t device_cctx_id_store(struct device *dev,
				    struct device_attribute *attr,
				    const char *buf,
				    size_t count)
{
	ssize_t rc = -EINVAL;
	priv_crypt_id_t id;

	printk(KERN_DEBUG "%s\n", __func__);

	if (sscanf(buf, "%d", &id) != 1)
		goto out;

	unuse_ctx(&dev->cctx_ref);

	if ((rc = use_ctx(id, &dev->cctx_ref)) < 0) {
		printk(KERN_ERR "%s: error in using cctx %d\n",
		       __func__, id);
		goto out;
	}

	dev->cctx_id = id;
	rc = count;

 out:
	printk(KERN_DEBUG "%s: %ld\n", __func__, rc);

	return rc;
}

int device_setup_cctx_attribute(struct device *dev)
{
	int rc = -1;
	struct device_attribute *dev_attr;

	dev_attr = kmalloc(sizeof(*dev_attr), GFP_KERNEL);
	if (!dev_attr) {
		rc = -ENOMEM;
		goto out;
	}

	dev_attr->attr.name = "crypt_ctx_id";
	dev_attr->attr.mode = 0600;
	dev_attr->show = device_cctx_id_show;
	dev_attr->store = device_cctx_id_store;

	rc = device_create_file(dev, dev_attr);
	if (rc < 0)
		goto out_free;

	cctx_ref_init(&dev->cctx_ref);
	dev->cctx_attr = dev_attr;

	rc = 0;
	goto out;

 out_free:
	kfree(dev_attr);
 out:
	return rc;
}

void device_destroy_cctx_attribute(struct device *dev)
{
	if (dev->cctx_attr) {
		device_remove_file(dev, dev->cctx_attr);
		kfree(dev->cctx_attr);
		dev->cctx_attr = NULL;

		unuse_ctx(&dev->cctx_ref);
		dev->cctx_id = 0;
	}
}
