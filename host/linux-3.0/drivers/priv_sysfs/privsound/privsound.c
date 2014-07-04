#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/kobject.h>
#include <linux/sysfs.h>
#include <linux/fs.h>
#include <linux/types.h>
#include <linux/slab.h>
#include "../privos_kobject.h"
#include <linux/priv_crypt.h>
#include <linux/rwlock.h>
#include <linux/time.h>

static struct kobject privsound_kobj;

#define PSND_IBUFLEN 1600
#define PSND_OBUFLEN 16000
#define PSND_IBUF_SIZE (PSND_IBUFLEN * 2 * sizeof(signed short))
#define PSND_OBUF_SIZE (PSND_OBUFLEN * 2 * sizeof(signed short))


static char *privsound_DMA_buffer_address;
static long privsound_DMA_size;

void set_DMA_buffer_address(char *dmabuf, long length) {
    privsound_DMA_buffer_address = dmabuf;
    privsound_DMA_size = length;
}
EXPORT_SYMBOL_GPL(set_DMA_buffer_address);

static rwlock_t privsound_llist_lock;

struct psnd_buf {
    char ibuffer[PSND_IBUF_SIZE];//input buffer
    char obuffer[PSND_OBUF_SIZE];//output buffer

    char name_buf[60];//to store the names of attributes

    priv_crypt_id_t out_ctx;
    struct priv_crypt_ctx_ref out_ref;
    priv_crypt_id_t in_ctx;
    struct priv_crypt_ctx_ref in_ref;
    unsigned int owptr;//pointer for user to write(output)
    unsigned int ofptr;//pointer for driver to fetch(output)

    unsigned int iwptr;//pointer for driver to write(input)
    unsigned int ifptr;//pointer for user fetch(input)

    int input_enabled;// 0: not enabled, 1: enabled
    
    struct bin_attribute attr_out;
    struct bin_attribute attr_out_avail;
    struct bin_attribute attr_in;
    struct psnd_buf* prev;
    struct psnd_buf* next;
};

static struct psnd_buf* psnd_head = NULL;

static inline int setup_cctx_refs(struct psnd_buf *pbuf,
        priv_crypt_id_t in, priv_crypt_id_t out)
{
    int rc = 0;
    pbuf->in_ctx = in;
    pbuf->out_ctx = out;
    cctx_ref_init(&pbuf->in_ref);
    if (use_ctx(in, &pbuf->in_ref) < 0) {
        printk(KERN_ERR "%s: use_ctx error\n", __func__);
        rc = -1;
        goto out;
    }
    cctx_ref_init(&pbuf->out_ref);
    if (use_ctx(out, &pbuf->out_ref) < 0) {
        printk(KERN_ERR "%s: use_ctx error\n", __func__);
        rc = -1;
        goto out_free;
    }
    goto out;
out_free:
    unuse_ctx(&pbuf->in_ref);
out:
    return rc;
}

static inline void unuse_cctx_refs(struct psnd_buf *pbuf)
{
    unuse_ctx(&pbuf->out_ref);
    unuse_ctx(&pbuf->in_ref);
}


static struct psnd_buf* find_psnd_buf(priv_crypt_id_t ctx) {
    unsigned long flags;
    struct psnd_buf* pbuf = psnd_head;
    read_lock_irqsave(&privsound_llist_lock, flags);
    while (pbuf) {
        if (pbuf->in_ctx == ctx || pbuf->out_ctx == ctx) {
            read_unlock_irqrestore(&privsound_llist_lock, flags);
            return pbuf;
        }
        pbuf = pbuf->next;
    }
    read_unlock_irqrestore(&privsound_llist_lock, flags);
    return NULL;
}

#if 0
//type: 0 in 1 out
static struct psnd_buf* find_psnd_buf_by_bin_attr(struct bin_attribute *attr,
        int type) {
    unsigned long flags;
    struct psnd_buf* pbuf = psnd_head;
    read_lock_irqsave(&privsound_llist_lock, flags);
    while (pbuf) {
        if ((type == 0 && &pbuf->attr_in == attr) || 
                    (type == 1 && &pbuf->attr_out == attr)) {
            read_unlock_irqrestore(&privsound_llist_lock, flags);
            return pbuf;
        }
        pbuf = pbuf->next;
    }
    read_unlock_irqrestore(&privsound_llist_lock, flags);
    return NULL;

}
#endif



/*
 * {en,de}crypt, direction determined by the ctx
 */
static inline void privsound_crypt(unsigned char *data, int len,
                                    struct priv_crypt_ctx_ref *ref)
{
    unsigned long flags;
    if (len <= 0)
        return;
#if 1
    spin_lock_irqsave(&ref->lock, flags);
    if (ref->cctx) {
        if (priv_crypt(ref->cctx, data, data, len) < 0) {
            printk(KERN_ERR "%s: crypt error!\n", __func__);
        }
    }
    spin_unlock_irqrestore(&ref->lock, flags);
#endif
#if 0
    //XXX simple XOR for testing now
    unsigned char *ptr = data;
    while (len-- > 0) {
        *ptr = *ptr ^ 0xa6;
        ptr++;
    }
#endif
}
                    



void privsound_encrypt_input(void *data, ssize_t len) {
    unsigned long flags;
    int avail, avail1, avail2, wp, fp;
    struct psnd_buf* pbuf = psnd_head;
    read_lock_irqsave(&privsound_llist_lock, flags);
    int i;

    while (pbuf) {
        if (!pbuf->input_enabled)
            goto next;
        wp = pbuf->iwptr;
        fp = pbuf->ifptr;
        if (wp < fp) {
            avail = fp - wp - 4;
            if (avail > len)
                avail = len;
            avail -= (avail%4);
            if (avail <= 0)
                goto next;
            memcpy(pbuf->ibuffer + wp, data, avail);
            for (i=0;i<avail;i+=4) {
                privsound_crypt(pbuf->ibuffer + wp + i,
                        4, &pbuf->in_ref);
            }
            pbuf->iwptr += avail;
            pbuf->iwptr %= PSND_IBUF_SIZE;
            wp = pbuf->iwptr;
        } else {
            avail1 = PSND_IBUF_SIZE - wp;
            avail = avail1 + fp - 4;
            if (avail > len)
                avail = len;
            avail -= (avail%4);
            if (avail1 > avail) {
                avail1 = avail;
                avail2 = 0;
            } else {
                avail2 = avail - avail1;
            }
            if (avail1 <= 0)
                goto next;
            memcpy(pbuf->ibuffer + wp, data, avail1);
            for (i=0;i<avail1;i+=4) {
                privsound_crypt(pbuf->ibuffer + wp + i,
                        4, &pbuf->in_ref);
            }
            pbuf->iwptr += avail1;
            pbuf->iwptr %= PSND_IBUF_SIZE;
            wp = pbuf->iwptr;
            if (avail2 <=0)
                goto next;
            memcpy(pbuf->ibuffer + wp, data, avail2);
            for (i=0;i<avail2;i+=4) {
                privsound_crypt(pbuf->ibuffer + wp + i,
                        4, &pbuf->in_ref);
            }
            pbuf->iwptr += avail2;
            pbuf->iwptr %= PSND_IBUF_SIZE;
        }
next:
        pbuf = pbuf->next;
    }

    //clear the dma buffer
    memset(data, 0, len);
    read_unlock_irqrestore(&privsound_llist_lock, flags);
}
EXPORT_SYMBOL_GPL(privsound_encrypt_input);

static ssize_t privsound_write_to_buf(const char *buffer, ssize_t size,
        struct psnd_buf* pbuf)
{
    int avail, avail1, avail2;
    int fp = pbuf->ofptr, wp = pbuf->owptr;

    if (size%4!=0)
        printk("PSND: write size error: %d\n",size);
    
    if (wp < fp) {
        avail1 = fp - wp - 4;
        if (avail1 > size)
            avail1 = size;
        memcpy(pbuf->obuffer + wp, buffer, avail1);

        
        pbuf->owptr += avail1;
        pbuf->owptr %= PSND_OBUF_SIZE;
        wp = pbuf->owptr;
        size = avail1;

    } else {
        avail1 = PSND_OBUF_SIZE - wp;
        avail2 = fp;
        avail = avail1 + avail2 - 4;
        if (avail > size)
            avail = size;
        if (avail1 > avail)
            avail1 = avail;
        memcpy(pbuf->obuffer + wp, buffer, avail1);


        pbuf->owptr += avail1;
        pbuf->owptr %= PSND_OBUF_SIZE;
        wp = pbuf->owptr;

        avail2 = avail - avail1;
        if (avail2 > 0) {
            if (wp)
                printk("PSND ERROR: wp = %d\n",wp);
            memcpy(pbuf->obuffer + 0, buffer + avail1, avail2);
            pbuf->owptr += avail2;
        }
        size = avail1 + avail2;
    }
    return size;

}

void privsound_buf_to_hw(void *buf, ssize_t frame_size, unsigned int frames)
{
    unsigned long flags;
    unsigned int num, frms, wp, fp;
    void *pbuffer;
    struct psnd_buf *pbuf;
    void *ptr;

    if (frames == 0)
        return;
    read_lock_irqsave(&privsound_llist_lock, flags);


    pbuf = psnd_head;
    while (pbuf) {
        pbuffer = pbuf->obuffer;

        wp = pbuf->owptr;
        fp = pbuf->ofptr;

        num = wp >= fp ? wp - fp : 
            PSND_OBUF_SIZE - fp + wp;
        frms = num / frame_size;
        ptr = buf;
        frms = frames > frms ? frms : frames;

        num = frms * frame_size;
        

        while (frms-- > 0) {
            //decrypt
            privsound_crypt(pbuffer+pbuf->ofptr, 4, &pbuf->out_ref);
            *(signed short *)ptr += *(signed short *)(pbuffer + pbuf->ofptr);
            *(signed short *)(ptr + frame_size / 2) 
                += *(signed short *)(pbuffer + pbuf->ofptr + frame_size / 2);

            //zero out the decrypted buffer
            *(signed int *)(pbuffer + pbuf->ofptr) = 0;
            pbuf->ofptr += frame_size;
            if (pbuf->ofptr >= PSND_OBUF_SIZE)
                pbuf->ofptr -= PSND_OBUF_SIZE;
            ptr += frame_size;
        }
        pbuf = pbuf->next;
    }

    read_unlock_irqrestore(&privsound_llist_lock, flags);
}
EXPORT_SYMBOL_GPL(privsound_buf_to_hw);


static ssize_t bin_write(struct file *fd, struct kobject *kobj, 
        struct bin_attribute *attr, char *buf, loff_t off, size_t count) {
    struct psnd_buf *pbuf = attr->private;
    return privsound_write_to_buf(buf, count, pbuf);
}

static ssize_t bin_read(struct file *fd, struct kobject *kobj, 
        struct bin_attribute *attr, char *buf, loff_t off, size_t count) {
    struct psnd_buf *pbuf = attr->private;
    int wp = pbuf->iwptr, fp = pbuf->ifptr, avail1, avail2;
    if (wp >= fp) {
        avail1 = wp - fp;
        avail2 = 0;
    } else {
        avail1 = PSND_IBUF_SIZE - fp;
        avail2 = wp;
    }
    if (avail1 > count) {
        avail1 = count;
    }
    memcpy(buf, pbuf->ibuffer + fp, avail1);
    count -= avail1;
    pbuf->ifptr += avail1;
    pbuf->ifptr %= PSND_IBUF_SIZE;
    fp = pbuf->ifptr;
    if (avail2 > count) {
        avail2 = count;
    }
    if (avail2 > 0) {
        memcpy(buf + avail1, pbuf->ibuffer + fp, avail2);
        pbuf->ifptr += avail2;
    }
    return avail1 + avail2;
}

static ssize_t bin_get_out_avail(struct file *fd, struct kobject *kobj, 
        struct bin_attribute *attr, char *buf, loff_t off, size_t count) {
    struct psnd_buf *pbuf = attr->private;
    ssize_t size = pbuf->owptr >= pbuf->ofptr ? 
            PSND_OBUF_SIZE - pbuf->owptr + pbuf->ofptr :
            pbuf->ofptr - pbuf->owptr;
    size = size - 4;
    *((int*)buf) = size / 4;
    return sizeof(int);
}

static int privsound_create_psnd_buf(priv_crypt_id_t in, priv_crypt_id_t out) {
    unsigned long flags;
    int name_len, rc = -1;
    struct psnd_buf* pbuf;

    pbuf = kmalloc(sizeof(struct psnd_buf), GFP_KERNEL);
    if (!pbuf) {
        rc = -ENOMEM;
        goto out;
    }

    name_len = sprintf(pbuf->name_buf, "output_%X", out);
    pbuf->name_buf[name_len] = 0;
    pbuf->attr_out.attr.name = pbuf->name_buf;
    pbuf->attr_out.attr.mode = S_IWUGO;
    pbuf->attr_out.private = pbuf;
    pbuf->attr_out.write = bin_write;
    
    name_len = sprintf(pbuf->name_buf + 20, "out_avail_%X", out);
    pbuf->name_buf[name_len + 20] = 0;
    pbuf->attr_out_avail.attr.name = pbuf->name_buf + 20;
    pbuf->attr_out_avail.attr.mode = S_IRUGO;
    pbuf->attr_out_avail.private = pbuf;
    pbuf->attr_out_avail.read = bin_get_out_avail;
    
    name_len = sprintf(pbuf->name_buf + 40, "input_%X", in);
    pbuf->name_buf[name_len + 40] = 0;
    pbuf->attr_in.attr.name = pbuf->name_buf + 40;
    pbuf->attr_in.attr.mode = S_IRUGO;
    pbuf->attr_in.private = pbuf;
    pbuf->attr_in.read = bin_read;
    
    pbuf->iwptr = 0;
    pbuf->ifptr = 0;
    pbuf->owptr = 0;
    pbuf->ofptr = 0;
    pbuf->next = NULL;
    pbuf->prev = NULL;
    pbuf->input_enabled = 0;


    rc = setup_cctx_refs(pbuf, in, out);
    if (rc < 0)
        goto out_free;
    sysfs_bin_attr_init(&pbuf->attr_in);
    sysfs_bin_attr_init(&pbuf->attr_out);
    sysfs_bin_attr_init(&pbuf->attr_out_avail);
    sysfs_create_bin_file(&privsound_kobj, &pbuf->attr_in);
    sysfs_create_bin_file(&privsound_kobj, &pbuf->attr_out);
    sysfs_create_bin_file(&privsound_kobj, &pbuf->attr_out_avail);

    write_lock_irqsave(&privsound_llist_lock, flags);
    if (psnd_head) {
        pbuf->next = psnd_head;
        psnd_head->prev = pbuf;
    }
    psnd_head = pbuf;
    write_unlock_irqrestore(&privsound_llist_lock, flags);
    
    goto out;

out_free:
    kfree(pbuf);
out:
    return rc;
}

static inline void privsound_free_psnd_buf(struct psnd_buf* pbuf) {
    memset(pbuf->obuffer, 0, PSND_OBUF_SIZE);
    memset(pbuf->ibuffer, 0, PSND_IBUF_SIZE);
    unuse_cctx_refs(pbuf);
    kfree(pbuf);
}

static void privsound_remove_psnd_buf(struct psnd_buf* pbuf) {
    unsigned long flags;
    if (!pbuf)
        return;
    sysfs_remove_bin_file(&privsound_kobj, &pbuf->attr_in);
    sysfs_remove_bin_file(&privsound_kobj, &pbuf->attr_out);
    sysfs_remove_bin_file(&privsound_kobj, &pbuf->attr_out_avail);
    
    write_lock_irqsave(&privsound_llist_lock, flags);
    if (pbuf->next) {
        pbuf->next->prev = pbuf->prev;
    }
    if (pbuf->prev) {
        pbuf->prev->next = pbuf->next;
    }
    else psnd_head = pbuf->next;

    write_unlock_irqrestore(&privsound_llist_lock, flags);
    privsound_free_psnd_buf(pbuf);

    //erase the DMA buffer
    if (privsound_DMA_buffer_address)
        memset(privsound_DMA_buffer_address, 0, privsound_DMA_size);
}

//register a new output path for a VM, write:
//<in_ctx_id>,<out_ctx_id>
//a new psnd_buf will be created
//out_ctx is used as part of the name for the new output channel(file)
//in_ctx is used as part of the name for the new input channel(file)
static struct attribute privsound_attr_register = {
    .name = "register",
    .mode = S_IWUGO,
};

static struct attribute privsound_attr_remove = {
    .name = "remove",
    .mode = S_IWUGO,
};

static struct attribute privsound_attr_show_pointers = {
    .name = "show_pointers",
    .mode = S_IRUGO,
};

static struct attribute privsound_attr_enable_input = {
    .name = "enable_input",
    .mode = S_IWUGO,
};

static struct attribute privsound_attr_disable_input = {
    .name = "disable_input",
    .mode = S_IWUGO,
};


static struct attribute* privsound_default_attrs[] = {
    &privsound_attr_register,
    &privsound_attr_remove,
    &privsound_attr_show_pointers,
    &privsound_attr_enable_input,
    &privsound_attr_disable_input,
    NULL
};



static inline int hex2int(char c) {
    if (c >= '0' && c <= '9')
        return c - '0';
    if (c >= 'a' && c <= 'f')
        return c - 'a' + 10;
    if (c >= 'A' && c <= 'F')
        return c- 'A' + 10;
    return -1;
}


static ssize_t privsound_store(struct kobject *kobj, struct attribute *attr,
        const char *buffer, size_t size)
{
    priv_crypt_id_t in = 0, out = 0, ctx = 0;
    ssize_t rc;
    struct psnd_buf* pbuf;
    int i;
    if (attr == &privsound_attr_register) {
        i = 0;
        while (i < size && hex2int(buffer[i]) >= 0) {
            in <<= 4;
            in += hex2int(buffer[i]);
            i++;
        }
        i++;
        while (i < size && hex2int(buffer[i]) >= 0) {
            out <<= 4;
            out += hex2int(buffer[i]);
            i++;
        }
        if (!find_psnd_buf(out) && !find_psnd_buf(in)) {
            rc = privsound_create_psnd_buf(in,out);
            if (rc < 0)
                size = rc;
        }
        else size = -EBUSY;
    }
    else if (attr == &privsound_attr_remove) {
        i = 0;
        while (i < size && hex2int(buffer[i]) >= 0) {
            ctx <<= 4;
            ctx += hex2int(buffer[i]);
            i++;
        }
        privsound_remove_psnd_buf(find_psnd_buf(ctx));
    }
    else if (attr == &privsound_attr_enable_input) {
        i = 0;
        while (i < size && hex2int(buffer[i]) >= 0) {
            ctx <<= 4;
            ctx += hex2int(buffer[i]);
            i++;
        }
        if((pbuf = find_psnd_buf(ctx))) {
            pbuf->input_enabled = 1;
        }
    }
    else if (attr == &privsound_attr_disable_input) {
        i = 0;
        while (i < size && hex2int(buffer[i]) >= 0) {
            ctx <<= 4;
            ctx += hex2int(buffer[i]);
            i++;
        }
        if((pbuf = find_psnd_buf(ctx))) {
            pbuf->input_enabled = 0;
        }
    }
    else return 0;
    return size;
}

static ssize_t privsound_show(struct kobject *kobj, struct attribute *attr,
        char *buffer)
{
    unsigned long flags;
    ssize_t size;
    struct psnd_buf* pbuf;
    if (attr == &privsound_attr_show_pointers) {
        size = 0;
        read_lock_irqsave(&privsound_llist_lock, flags);
        pbuf = psnd_head;
        while (pbuf) {
            size += sprintf(buffer + size, "in_ctx_id: %X, out_ctx_id: %X, \
iwptr: %d, ifptr: %d owptr: %d, ofptr: %d\n", pbuf->in_ctx, pbuf->out_ctx, 
pbuf->iwptr, pbuf->ifptr, pbuf->owptr, pbuf->ofptr);
            pbuf = pbuf->next;
        }
        read_unlock_irqrestore(&privsound_llist_lock, flags);
        return size;
    }

    return 0;
}

static struct sysfs_ops privsound_sysfs_ops = {
    .show = &privsound_show,
    .store = &privsound_store
};

static struct kobj_type privsound_ktype = {
    .sysfs_ops = &privsound_sysfs_ops,
    .default_attrs = privsound_default_attrs
};


int privsound_init(void)
{
    kobject_init_and_add(&privsound_kobj, &privsound_ktype, 
            &privos_kobj, "privsound");
    rwlock_init(&privsound_llist_lock);
    return 0;
}

void privsound_exit(void)
{
    unsigned long flags;
    struct psnd_buf *pbuf, *pbuf_tmp;
    write_lock_irqsave(&privsound_llist_lock, flags);

    pbuf = psnd_head;
    while (pbuf) {
        pbuf_tmp = pbuf->next;
        sysfs_remove_bin_file(&privsound_kobj, &pbuf->attr_in);
        sysfs_remove_bin_file(&privsound_kobj, &pbuf->attr_out);
        sysfs_remove_bin_file(&privsound_kobj, &pbuf->attr_out_avail);
        privsound_free_psnd_buf(pbuf);
        pbuf = pbuf_tmp;
    }
    write_unlock_irqrestore(&privsound_llist_lock, flags);

    kobject_del(&privsound_kobj);
    kobject_put(&privsound_kobj);
}
module_init(privsound_init);
module_exit(privsound_exit);

MODULE_LICENSE("GPL");
