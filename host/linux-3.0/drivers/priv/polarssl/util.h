#ifndef _POLARSSL_UTIL_H_
#define _POLARSSL_UTIL_H_

#include <linux/slab.h>

#define printf(fmt, args...) printk(KERN_DEBUG fmt, ##args)

static inline void *malloc(size_t sz)
{
    return kmalloc(sz, GFP_KERNEL);
}

static inline void free(void *ptr)
{
    kfree(ptr);
}

#endif
