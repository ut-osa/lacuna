#ifndef _LINUX_PRIVOS_H
#define _LINUX_PRIVOS_H

#define PRIVOS_CIPHER_KEY_SIZE 32
#define PRIVOS_CIPHER_IV_SIZE 16
#define PRIVOS_CIPHER_ALG "ctr(aes)"

#include <linux/gfp.h>
#include <linux/atomic.h>
#include <linux/spinlock.h>
#include <linux/radix-tree.h>

/* per mm_struct ctx to track the cipher */
struct priv_cipher_ctx {
	spinlock_t lock;
	atomic_t pagecount;
	atomic_t mapcount;
	struct blkcipher_desc *cipher;
	struct mm_struct *mm;
};

/* per swapped out page struct */
struct priv_swapped_page {
	struct priv_cipher_ctx *ctx;
	struct page *page;
	struct page *scratch;
	unsigned long entry;
	unsigned int flags;
};

struct priv_print_flags {
   int mask;
   char *name;
};

extern void priv_swap_setup(void);

extern struct priv_swapped_page *find_priv_page(unsigned long);
extern int add_to_priv(struct page *);
extern void delete_all_from_priv(struct priv_cipher_ctx *);
extern void delete_from_priv(unsigned long);
extern void delete_scratch_page(unsigned long);

extern void set_swapped_bit(uint64_t, int);
extern int test_swapped_bit(uint64_t);

extern struct priv_cipher_ctx *priv_ctx_alloc(void);
extern int priv_ctx_get(struct priv_cipher_ctx *);
extern void priv_ctx_put(struct priv_cipher_ctx *);

extern struct page *priv_encrypt_page(struct page *);
extern void priv_decrypt_page(struct page *);

#ifdef CONFIG_PRIV_DEBUG
#define __priv_info(...) printk(KERN_INFO __VA_ARGS__)
#define __priv_debug(...) printk(KERN_DEBUG __VA_ARGS__)
#else
#define __priv_info(...) do {} while(0)
#define __priv_debug(...) do {} while(0)
#endif

#ifdef CONFIG_DEATH_SCAN
int match_pattern(u8 *, int);
void scan_physical(void);
#endif

#endif
