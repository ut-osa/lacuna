#include <linux/module.h>
#include <linux/cdev.h>
#include <linux/fs.h>
#include <linux/slab.h>
#include <linux/uaccess.h>
#include <linux/list.h>
#include <linux/spinlock.h>
#include <linux/types.h>
#include <linux/sched.h>
#include <linux/mutex.h>

#include <linux/priv_crypt.h>
#include <linux/priv_crypt_kern.h>
#include "priv_crypt_int.h"

static const char DRV_NAME[] = "priv_crypt";

struct priv_crypt_config {
	dev_t dev;
	struct cdev cdev;

	u8 init;
	struct mutex ctx_list_lock;
	struct list_head ctx_list;

#ifdef PRIV_CRYPT_DEBUG
	struct priv_crypt_crypt_desc enc_desc;
#endif
};

static struct priv_crypt_config the_config = {
	.init = 0
};

void cctx_ref_init(struct priv_crypt_ctx_ref *cctx_ref)
{
	spin_lock_init(&cctx_ref->lock);
	cctx_ref->cctx = NULL;
}
EXPORT_SYMBOL_GPL(cctx_ref_init);

static struct priv_crypt_ctx *
__find_ctx_by_id(priv_crypt_id_t id,
		 struct list_head *ctx_list)
{
	struct priv_crypt_ctx *cctx;

	list_for_each_entry(cctx, ctx_list, list) {
		if (cctx->id == id)
			goto out;
	}

	cctx = NULL;

 out:
	return cctx;
}

static struct priv_crypt_ctx *find_ctx_by_id(priv_crypt_id_t id)
{
	struct priv_crypt_ctx *cctx = NULL;
	struct list_head *ctx_list = &the_config.ctx_list;
	struct mutex *ctx_list_lock = &the_config.ctx_list_lock;
	
	mutex_lock(ctx_list_lock);
	cctx = __find_ctx_by_id(id, ctx_list);
	mutex_unlock(ctx_list_lock);

	return cctx;
}

int use_ctx(priv_crypt_id_t id, struct priv_crypt_ctx_ref *cctx_ref)
{
	int rc = -ENODEV;
	struct priv_crypt_ctx *cctx;

	if (!cctx_ref) {
		rc = -EINVAL;
		goto out;
	}

	cctx = find_ctx_by_id(id);
	if (!cctx) {
		rc = -ENODEV;
		goto out;
	}
	
	if (mutex_trylock(&cctx->user_lock)) {
		/* got the usage lock */
		unsigned long flags;

		spin_lock_irqsave(&cctx_ref->lock, flags);

		cctx->user_ref = cctx_ref;
		cctx_ref->cctx = cctx;

		spin_unlock_irqrestore(&cctx_ref->lock, flags);

		rc = 0;
		goto out;
	} else {
		rc = -EBUSY;
		goto out;
	}

 out:
	return rc;
}
EXPORT_SYMBOL_GPL(use_ctx);

void unuse_ctx(struct priv_crypt_ctx_ref *cctx_ref)
{
	struct priv_crypt_ctx *cctx;
	unsigned long flags;

	if (!cctx_ref)
		return;

	spin_lock_irqsave(&cctx_ref->lock, flags);

	cctx = cctx_ref->cctx;
	if (!cctx)
		goto unlock;

	cctx->user_ref = NULL;
	mutex_unlock(&cctx->user_lock);

 unlock:
	spin_unlock_irqrestore(&cctx_ref->lock, flags);
}
EXPORT_SYMBOL_GPL(unuse_ctx);

static void __store_ctx(struct priv_crypt_ctx *ctx,
			struct list_head *ctx_list)
{
	list_add(&ctx->list, ctx_list);	
}

static void store_ctx(struct priv_crypt_ctx *ctx)
{
	struct list_head *ctx_list = &the_config.ctx_list;
	struct mutex *ctx_list_lock = &the_config.ctx_list_lock;

	if (ctx) {
		mutex_lock(ctx_list_lock);
		__store_ctx(ctx, ctx_list);
		mutex_unlock(ctx_list_lock);
	}
}

static void __remove_ctx(struct priv_crypt_ctx *ctx)
{
	if (ctx) {
		if (ctx->user_ref) {
			unsigned long flags;
			spin_lock_irqsave(&ctx->user_ref->lock, flags);
			/* after next line, potential holder of user lock will
			   not be able to use the context any more */
			ctx->user_ref->cctx = NULL;
			spin_unlock_irqrestore(&ctx->user_ref->lock, flags);

			ctx->user_ref = NULL;
		}

		list_del(&ctx->list);
	}
}

static void remove_ctx(struct priv_crypt_ctx *ctx)
{
	struct mutex *ctx_list_lock = &the_config.ctx_list_lock;

	mutex_lock(ctx_list_lock);
	__remove_ctx(ctx);
	mutex_unlock(ctx_list_lock);
}

static int priv_crypt_open(struct inode *in, struct file *f)
{
	f->private_data = NULL;

	return 0;
}

static int priv_crypt_release(struct inode *in, struct file *f)
{
	return 0;
}

static long do_new_ctx(struct priv_crypt_ctx **priv_store, void __user *arg)
{
	int rc = -EINVAL;
	struct priv_crypt_alg_desc desc;
	struct priv_crypt_ctx *cctx;

	if (copy_from_user(&desc, arg, sizeof(desc))) {
		rc = -EFAULT;
		goto out;
	}

	rc = make_crypto_ctx(&cctx, desc.alg, (size_t)desc.key_size, desc.dir);
	if (rc < 0)
		goto out;
	cctx->alloc_pid = current->pid;

	printk(KERN_DEBUG "new priv_crypt_ctx(%p): "
	       "alg %lu key_size %lu "
	       "pid %lu\n",
               cctx,
	       (unsigned long)desc.alg, (unsigned long)desc.key_size,
	       (unsigned long)cctx->alloc_pid);

	store_ctx(cctx);
	*priv_store = cctx;
        /* rc from make_crypto_ctx is the new id */

 out:
	return rc;
}

/* XXX: only allow one user from userspace at a time? */
static long do_use_ctx(struct priv_crypt_ctx **priv_store, void __user *arg)
{
	int rc = -EINVAL;
	priv_crypt_id_t id;
	struct priv_crypt_ctx *cctx;

	if (copy_from_user(&id, arg, sizeof(id))) {
		rc = -EFAULT;
		goto out;
	}

	cctx = find_ctx_by_id(id);
	if (!cctx) {
		rc = -ENODEV;
		goto out;
	}

	*priv_store = cctx;
	rc = 0;

 out:
	return rc;
}

static long do_set_iv(struct priv_crypt_ctx *cctx, void __user *arg)
{
	int rc = -EINVAL;
	struct priv_crypt_iv_desc desc;

	if (!cctx)
		goto out;

	if (copy_from_user(&desc, arg, sizeof(desc))) {
		rc = -EFAULT;
		goto out;
	}

	rc = priv_set_iv(cctx, desc.iv_size, desc.iv);

 out:
	return rc;
}

static long do_set_activated(struct priv_crypt_ctx *cctx, int activated)
{
	int rc = -EINVAL;

	if (!cctx)
		goto out;

	priv_set_activated(cctx, activated);
	rc = 0;

 out:
	return rc;
}

static long do_destroy(struct priv_crypt_ctx **priv_store)
{
	int rc = -EINVAL;

	if (!*priv_store)
		goto out;

	remove_ctx(*priv_store);
	destroy_crypto_ctx(*priv_store);

	*priv_store = NULL;
	rc = 0;

 out:
	return rc;
}

/* destroy cctxs allocated from a certain pid */
void priv_pid_death_action(pid_t pid)
{
	struct priv_crypt_ctx *cctx, *tmp;
	struct list_head *ctx_list = &the_config.ctx_list;
	struct mutex *ctx_list_lock = &the_config.ctx_list_lock;

	if (!the_config.init)
		return;

	mutex_lock(ctx_list_lock);

	list_for_each_entry_safe(cctx, tmp, ctx_list, list) {
		if (cctx->alloc_pid == pid) {
			__remove_ctx(cctx);
			destroy_crypto_ctx(cctx);
		}
	}

	mutex_unlock(ctx_list_lock);
}

#ifdef PRIV_CRYPT_DEBUG
static long do_crypt(struct priv_crypt_ctx *cctx, void __user *arg)
{
	int rc = -EINVAL;
        /* Having only one of these buffers messes up locking, but
           this is a testing command anyway */
	struct priv_crypt_crypt_desc *desc = &the_config.enc_desc;

	if (!cctx)
		goto out;

	if (copy_from_user(desc, arg, sizeof(*desc))) {
		rc = -EFAULT;
		goto out;
	}

	rc = priv_crypt(cctx, desc->data, desc->data, desc->data_size);
	if (rc < 0)
		goto out;

	if (copy_to_user((void __user *)&((struct priv_crypt_crypt_desc *)arg)->data,
			 desc->data,
			 desc->data_size)) {
		rc = -EFAULT;
		goto out;
	}

	rc = 0;

 out:
	return rc;
}
#else
static long do_crypt(struct priv_crypt_ctx *cctx, void __user *arg)
{
	printk(KERN_ERR "debug mode is not enabled\n");
	return -EINVAL;
}
#endif

static long do_kex(struct priv_crypt_ctx *cctx, void __user *arg)
{
	int rc = -1;
	struct priv_crypt_kex_msg_desc kex_msg_desc;
	size_t out_sz;
	u32 out_sz_rep;

	if (copy_from_user(&kex_msg_desc, arg, sizeof(kex_msg_desc))) {
		rc = -EFAULT;
		goto out;
	}

	if (kex_msg_desc.in_size > MAX_KEX_MSG_LEN) {
		rc = -EINVAL;
		goto out;
	}

	if (copy_from_user(cctx->kex_buf,
			   kex_msg_desc.in_buf,
			   kex_msg_desc.in_size)) {
		rc = -EFAULT;
		goto out;
	}

	out_sz = MAX_KEX_MSG_LEN;
	rc = process_kex_msg(cctx,
			     &out_sz, cctx->kex_buf,
			     kex_msg_desc.in_size, cctx->kex_buf);
	if (rc < 0)
		goto out;

	out_sz_rep = (u32)out_sz;
	if (copy_to_user((void __user *)&((struct priv_crypt_kex_msg_desc *)arg)->out_size,
			 &out_sz_rep, sizeof(out_sz_rep))) {
		rc = -EFAULT;
		goto out;
	}
	if (copy_to_user((void __user *)kex_msg_desc.out_buf,
			 cctx->kex_buf, out_sz)) {
		rc = -EFAULT;
		goto out;
	}
	rc = 0;

 out:
	return rc;
}

static long priv_crypt_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
	int rc = -EINVAL;

        printk(KERN_DEBUG "priv crypt command %lu (%p in %p), arg = %lu",
               (unsigned long)cmd,
               file->private_data, &file->private_data,
               arg);

	if (!the_config.init) {
		printk(KERN_ERR "%s: not yet initialized!\n", __func__);
		BUG();
	}

	switch (cmd) {
	case PRIV_CRYPT_CMD_NEW_CTX:
		rc = do_new_ctx((struct priv_crypt_ctx **)&file->private_data,
				(void __user *)arg);
		break;
	case PRIV_CRYPT_CMD_USE_CTX:
		rc = do_use_ctx((struct priv_crypt_ctx **)&file->private_data,
				(void __user *)arg);
		break;
	case PRIV_CRYPT_CMD_SET_IV:
		rc = do_set_iv((struct priv_crypt_ctx *)file->private_data,
			       (void __user *)arg);
		break;
	case PRIV_CRYPT_CMD_ACTIVATE:
		rc = do_set_activated((struct priv_crypt_ctx *)file->private_data,
				      1);
		break;
	case PRIV_CRYPT_CMD_DEACTIVATE:
		rc = do_set_activated((struct priv_crypt_ctx *)file->private_data,
				      0);
		break;
	case PRIV_CRYPT_CMD_DESTROY:
		rc = do_destroy((struct priv_crypt_ctx **)&file->private_data);
		break;
	case PRIV_CRYPT_CMD_DEBUG_CRYPT:
		rc = do_crypt((struct priv_crypt_ctx *)file->private_data,
			      (void __user *)arg);
		break;
	case PRIV_CRYPT_CMD_KEY_EXCHANGE:
		rc = do_kex((struct priv_crypt_ctx *)file->private_data,
			    (void __user *)arg);
	default:
		break;
	}

	return rc;
}

static struct file_operations priv_crypt_fops = {
	.open = priv_crypt_open,
	.release = priv_crypt_release,
	.unlocked_ioctl = priv_crypt_ioctl,
};

static int priv_crypt_init(void)
{
	int rc = -1;
	struct priv_crypt_config *config = &the_config;

	INIT_LIST_HEAD(&config->ctx_list);
	mutex_init(&config->ctx_list_lock);
	/* XXX: barrier? */
	config->init = 1;

	if (alloc_chrdev_region(&config->dev, 0, 1, DRV_NAME) < 0)
		goto out;

	cdev_init(&config->cdev, &priv_crypt_fops);
	if (cdev_add(&config->cdev, config->dev, 1) < 0) {
		printk(KERN_ERR "%s: Couldn't add device\n", DRV_NAME);
		goto out_dealloc;
	}

	rc = 0;
	goto out;

 out_dealloc:
	unregister_chrdev_region(config->dev, 1);
 out:
	return rc;
}

static void priv_crypt_exit(void)
{
	struct priv_crypt_config *config = &the_config;

	unregister_chrdev_region(config->dev, 1);

	/* XXX: destroy all cctx (then this could be a proper module) */
}

module_init(priv_crypt_init);
module_exit(priv_crypt_exit);

MODULE_LICENSE("GPL");
