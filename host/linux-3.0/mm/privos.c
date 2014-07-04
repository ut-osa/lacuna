/*
 *  linux/mm/privos.c
 *
 *  Tracking the privos cipher contexts
 */
#include <linux/mm.h>
#include <linux/page-flags.h>
#include <linux/swap.h>
#include <linux/rmap.h>

#include <linux/privos.h>
#include <linux/crypto.h>
#include <crypto/rng.h>
#include <linux/scatterlist.h>

#include <linux/sched.h>
#include <linux/wait.h>
#include <linux/atomic.h>
#include <linux/types.h>
#include <asm/atomic64_64.h>

#include "privos_stats.h"
__inline__ uint64_t rdtsc(void) {
	uint32_t lo, hi;
	__asm__ __volatile__ (
    "        xorl %%eax,%%eax \n"
    "        cpuid"
    ::: "%rax", "%rbx", "%rcx", "%rdx");
	__asm__ __volatile__ ("rdtsc" : "=a" (lo), "=d" (hi));
	return (uint64_t)hi << 32 | lo;
}
time_vars(add_priv);
time_vars(delete_priv);
time_vars(encrypt);
time_vars(decrypt);
time_vars(level_three);

atomic64_t fail_count = ATOMIC64_INIT(0);
atomic64_t fallback_count = ATOMIC64_INIT(0);

void show_counters(void);

#ifdef CONFIG_PRIV_SWAP

DEFINE_SPINLOCK(privos_tree_lock);
RADIX_TREE(privos_tree, GFP_ATOMIC|__GFP_NOWARN);
static struct kmem_cache *swpd_cachep;
static struct kmem_cache *scratch_cachep;

static void swpd_ctor(void *data) {}

static void *__get_scratch_page(void)
{
	void *buf;

	buf = kmem_cache_zalloc(scratch_cachep, GFP_KERNEL);
	return buf;
}

static void __free_scratch_page(void *buf)
{
	if (buf)
		kmem_cache_free(scratch_cachep, buf);
}

static int priv_ctx_entry_get(struct priv_cipher_ctx *ctx) {
	if (!ctx)
		return 0;
	atomic_inc(&ctx->pagecount);
	return 1;
}

static void priv_ctx_entry_put(struct priv_cipher_ctx *ctx)
{
	if (!ctx)
		return;
	/* We want to lock and turn off interrupts */
	local_irq_disable();
	preempt_disable();
	if (0 == atomic_read(&ctx->mapcount)) {
		if (atomic_dec_and_lock(&ctx->pagecount, &ctx->lock)) {
			__priv_debug("Doing the final ctx free %p\n", ctx);
			spin_unlock(&ctx->lock);
			memset(ctx, 0 , sizeof(*ctx));
			kfree(ctx);
		}
	} else {
		atomic_dec(&ctx->pagecount);
	}
	local_irq_enable();
	preempt_enable();
}

static void __free_priv_page(struct priv_swapped_page *swpd)
{
	if (swpd) {
		priv_ctx_entry_put(swpd->ctx);
		if (swpd->scratch) {
			__free_scratch_page(page_address(swpd->scratch));
		}
		memset(swpd, 0, sizeof(*swpd));
		kmem_cache_free(swpd_cachep, swpd);
	}
}

static struct priv_swapped_page *__alloc_priv_page(struct page *page,
						struct priv_cipher_ctx *ctx)
{
	struct priv_swapped_page *swpd = NULL;
	void *buf = NULL;
	time_start(level_three);

	if (!priv_ctx_entry_get(ctx))
		goto out;

	buf = __get_scratch_page();
	if (!buf) {
		printk(KERN_ERR "Allocation failed!\n");
		goto out;
	}
	time_end(level_three);

	swpd = kmem_cache_zalloc(swpd_cachep, GFP_NOWAIT);

	if (!swpd) {
		__free_scratch_page(buf);
		goto out;
	}

	swpd->ctx = ctx;
	swpd->page = page;
	swpd->entry = page_private(page);
	swpd->scratch = NULL;
	swpd->flags = 0;
	if (buf) {
		swpd->scratch = virt_to_page(buf);
		set_page_private(swpd->scratch, page_private(page));
	}
out:
	return swpd;
}

static int __add_to_priv_tree(struct priv_swapped_page *swpd)
{
	int err, ret = 0;
	unsigned long entry;

	if (!swpd)
		goto out;
	entry = page_private(swpd->page);
	if (!entry)
		goto out;

	err = radix_tree_preload(GFP_NOWAIT);
	if (!err) {
		spin_lock_irq(&privos_tree_lock);
	try_again:
		err = radix_tree_insert(&privos_tree, entry, swpd);

		if (unlikely(err)) {
			printk(KERN_ERR "Sh%dt...\n", -err);
			if (err == -EEXIST) {
				struct priv_swapped_page *old_swpd;
				printk(KERN_ERR "Double insert %ld\n", entry);
				old_swpd = radix_tree_delete(&privos_tree, entry);
				__free_priv_page(old_swpd);
				/* No retry limit */
				goto try_again;
			}
			__free_priv_page(swpd);
			goto out_lock;
		}
	} else {
		printk(KERN_ERR "radix tree preload error\n");
		ret = 0;
		goto out;
	}
	ret = 1;
out_lock:
	spin_unlock_irq(&privos_tree_lock);
	radix_tree_preload_end();
out:
	return ret;
}

static int __add_to_priv(struct page *page)
{
	struct anon_vma *anon_vma;
	struct anon_vma_chain *avc;
	struct vm_area_struct *vma = NULL;

	struct priv_swapped_page *swpd;
	int count = 0, ret = 0;

	VM_BUG_ON(!PageLocked(page));
	VM_BUG_ON(!PageUptodate(page));
	VM_BUG_ON(!PageSwapCache(page));

	if (page_mapcount(page) > 1)
		return 1;
	if (!page_mapped(page) || !page_rmapping(page)) {
		return 1;
	}

	anon_vma = page_lock_anon_vma(page);
	if (!anon_vma) {
		/* This is more a warning bell of something weird.  I
		 * don't think it's incorrect */
		return 1;
	}

	/* If there's more than one vma, should we check if the
	 * mm_struct matches? */
	list_for_each_entry(avc, &anon_vma->head, same_anon_vma) {
		if (vma && vma->vm_mm != avc->vma->vm_mm) {
			/* Maybe we can still swap this out */
			__priv_info("priv_ctx don't match %p %p\n",
				vma->vm_mm->priv_ctx, avc->vma->vm_mm->priv_ctx);
			ret = 1;
			goto out;
		}
		vma = avc->vma;
		count += 1;
		if (vma->priv_ctx == NULL) {
			ret = 1;
			goto out;
		}
	}

	if (!count) {
		ret = 1;
		goto out;
	}

	swpd = __alloc_priv_page(page, vma->priv_ctx);
	if (!swpd) {
		ret = 0;
		goto out;
	}
	if (!__add_to_priv_tree(swpd)) {
		__free_priv_page(swpd);
		ret = 0;
		goto out;
	}

	ret = 1;
out:
	page_unlock_anon_vma(anon_vma);

	return ret;
}

int test_swapped_bit(uint64_t offset)
{
	int rc = 0;
	struct priv_swapped_page *swpd = NULL;
	swpd = find_priv_page(offset);
	if (swpd) {
		rc = !!(swpd->flags);
	}
	return rc;
}

void set_swapped_bit(uint64_t offset, int val)
{
	struct priv_swapped_page *swpd = NULL;
	swpd = find_priv_page(offset);
	if (swpd) {
		swpd->flags = val;
	}
}

void __init priv_swap_setup()
{
	swpd_cachep = kmem_cache_create("swapped_page", sizeof(struct priv_swapped_page),
					0, SLAB_PANIC, swpd_ctor);
	scratch_cachep = kmem_cache_create("scratch_page", PAGE_SIZE,
					   0, SLAB_PANIC, swpd_ctor);
}

/**
 * find_priv_page - find and get a page reference
 * @mapping: the address_space to search
 * @offset: the page index
 *
 * Is there a pagecache struct page at the given (mapping, offset) tuple?
 * If yes, increment its refcount and return it; if no, return NULL.
 */
struct priv_swapped_page *find_priv_page(unsigned long entry)
{
	void **pagep;
	struct priv_swapped_page *page = NULL;

	rcu_read_lock();
repeat:
	page = NULL;
	pagep = radix_tree_lookup_slot(&privos_tree, entry);
	if (pagep) {
		page = radix_tree_deref_slot(pagep);
		if (unlikely(!page))
			goto out;
		if (radix_tree_deref_retry(page))
			goto repeat;
		if (unlikely(page != *pagep)) {
			printk(KERN_ERR "WTF? Don't know what this means...\n");
			goto repeat;
		}
		BUG_ON(page->entry != entry);
	}
out:
	rcu_read_unlock();
	return page;
}

/**
 * delete_all_from_priv - find and remove all private pages
 */
void delete_all_from_priv(struct priv_cipher_ctx *ctx)
{
	unsigned long index = 0;
	struct priv_swapped_page *victims[64] = {0};
	int i, found;

	do {
		spin_lock_irq(&privos_tree_lock);
		found = radix_tree_gang_lookup(&privos_tree, (void **)&victims,
					index, 64);
		spin_unlock_irq(&privos_tree_lock);
		for (i = 0; i < found; i++) {
			if (test_swapped_bit(victims[i]->entry))
				set_swapped_bit(victims[i]->entry, 0);
			if (victims[i]->page)
				swapcache_free((swp_entry_t){victims[i]->entry},
					       victims[i]->page);
		}
		if (found)
			index = victims[found - 1]->entry + 1;
	} while (found);
}

/**
 * delete_from_priv - find and track the private pages
 * @entry: the entry we want to remove
 */
void delete_from_priv(unsigned long entry)
{
	struct priv_swapped_page *swpd;
	if (entry && !test_swapped_bit(entry)) {
		time_start(delete_priv);
		spin_lock_irq(&privos_tree_lock);
		swpd = radix_tree_delete(&privos_tree, entry);
		spin_unlock_irq(&privos_tree_lock);
		__free_priv_page(swpd);
		time_end(delete_priv);
	}
}

/**
 * delete_scratch_page - remove the associated scratch page
 * @entry: the scratch page we want to remove
 */
void delete_scratch_page(unsigned long entry)
{
	struct priv_swapped_page *swpd;
	if (entry) {
		swpd = find_priv_page(entry);
		if (swpd && swpd->flags && swpd->scratch) {
			__free_scratch_page(page_address(swpd->scratch));
			swpd->scratch = NULL;
			swpd->page = NULL;
		}
	}
}

/**
 * add_to_priv - find and track the private pages
 * @page: page we want to encrypt
 *
 * Find the controlling mm to see if we need to
 * encrypt.  Caller needs to hold the page lock.
 *
 * TODO: Clean up and make consistent the error
 *       paths.  There are two interweaving possibilities
 *       it fixes up as if finds out more.
 */
int add_to_priv(struct page *page)
{
	int rc = 1;
	time_start(add_priv);

	rc = __add_to_priv(page);

	time_end(add_priv);
	return rc;
}

/**
 * priv_ctx_alloc
 */
struct priv_cipher_ctx *priv_ctx_alloc(void)
{
	char key[PRIVOS_CIPHER_KEY_SIZE] = {0};
	struct priv_cipher_ctx *ctx = NULL;

	if (!crypto_default_rng && crypto_get_default_rng()) {
		printk(KERN_ERR "Err: default rng\n");
		return NULL;
	} else {
		crypto_rng_get_bytes(crypto_default_rng, key,
				PRIVOS_CIPHER_KEY_SIZE);
	}

	ctx = kzalloc(sizeof(*ctx), GFP_KERNEL);
	if (!ctx)
		return NULL;

	atomic_set(&ctx->mapcount, 1);
	atomic_set(&ctx->pagecount, 0);
	spin_lock_init(&ctx->lock);
	ctx->cipher = kzalloc(sizeof(*ctx->cipher), GFP_KERNEL);
	ctx->cipher->tfm = crypto_alloc_blkcipher(PRIVOS_CIPHER_ALG, 0, 0);
	crypto_blkcipher_setkey(ctx->cipher->tfm, key, PRIVOS_CIPHER_KEY_SIZE);

	return ctx;
}


/**
 * priv_ctx_get: priv_ctx reference counting
 */
int priv_ctx_get(struct priv_cipher_ctx *ctx) {
	int rc = 1;

	if (!ctx)
		return 0;
	/* We want to lock and turn off interrupts */
	local_irq_disable();
	preempt_disable();

	if (!atomic_inc_not_zero(&ctx->mapcount)) {
		rc = 0;
	}

	local_irq_enable();
	preempt_enable();

	return rc;
}

/**
 * priv_ctx_put: priv_ctx reference counting
 */
void priv_ctx_put(struct priv_cipher_ctx *ctx) {
	char null_key[PRIVOS_CIPHER_KEY_SIZE] = {0};
	ktime_t _start, _end;

	if (!ctx)
		return;
	/* We want to lock and turn off interrupts */
	local_irq_disable();
	preempt_disable();

	if (atomic_dec_and_lock(&ctx->mapcount, &ctx->lock)) {
		__priv_debug("Doing the crypto ctx free %p\n", ctx->cipher);
		_start = ktime_get();
		delete_all_from_priv(ctx);
		_end = ktime_get();
		printk(KERN_ERR "swapcache_clear %ld\n",
		       (long int)(_end.tv64 - _start.tv64));

		crypto_blkcipher_setkey(ctx->cipher->tfm, null_key,
					PRIVOS_CIPHER_KEY_SIZE);
		crypto_free_blkcipher(ctx->cipher->tfm);
		kfree(ctx->cipher);
		ctx->cipher = NULL;
		atomic_set(&ctx->mapcount, 0);

		__priv_debug("ctx->pagecount %d\n", atomic_read(&ctx->pagecount));
		show_counters();
		/* Traverse radix tree and delete? */
		if (0 == atomic_read(&ctx->pagecount)) {
			__priv_debug("Doing the final ctx free %p\n", ctx);
			spin_unlock(&ctx->lock);
			memset(ctx, 0 , sizeof(*ctx));
			kfree(ctx);
		} else {
			spin_unlock(&ctx->lock);
		}
	}

	local_irq_enable();
	preempt_enable();
}

struct page *priv_encrypt_page(struct page *page)
{
	struct priv_swapped_page *swpd;
	struct priv_cipher_ctx *ctx;
	struct scatterlist sgl[2];
	char iv[PRIVOS_CIPHER_IV_SIZE] = {0};
	unsigned long entry = page_private(page);

	swpd = find_priv_page(entry);
	if (swpd && (ctx = swpd->ctx)) {
		time_start(encrypt);
		if (page != swpd->page) {
			printk(KERN_ERR "stored page out of date, can't turn back now...\n");
			set_swapped_bit(entry, 0);
			delete_from_priv(entry);
			return page;
		}
		if (!swpd->scratch) {
			set_swapped_bit(entry, 0);
			delete_from_priv(entry);
			return page;
		}
		__priv_debug("Encrypt page %p -> %lx\n", page, entry);
		BUG_ON(page != swpd->page);
		BUG_ON(!swpd->scratch);
		BUG_ON(entry != page_private(swpd->scratch));

		sg_init_table(sgl, 2);
		sg_set_page(&sgl[1], swpd->scratch, PAGE_SIZE, 0);
		sg_set_page(&sgl[0], page, PAGE_SIZE, 0);

		spin_lock_irq(&ctx->lock);
		crypto_blkcipher_set_iv(ctx->cipher->tfm, iv, PRIVOS_CIPHER_IV_SIZE);
#ifdef CONFIG_PRIV_CRYPT
		crypto_blkcipher_encrypt(ctx->cipher, &sgl[1], &sgl[0], PAGE_SIZE);
#endif
		spin_unlock_irq(&ctx->lock);

		set_swapped_bit(entry, 1);
		time_end(encrypt);

#ifdef CONFIG_PRIV_CRYPT
		return swpd->scratch;
#endif
	}

	return page;
}

void priv_decrypt_page(struct page *page)
{
	char iv[PRIVOS_CIPHER_IV_SIZE] = {0};
	struct priv_swapped_page *swpd;
	struct priv_cipher_ctx *ctx;
	struct scatterlist sg;

	swpd = find_priv_page(page_private(page));
	if (swpd && (ctx = swpd->ctx) && atomic_read(&swpd->ctx->mapcount)) {
		time_start(decrypt);
		__priv_debug("Decrypt page %p -> %lx\n", page,
			page_private(page));
		sg_init_table(&sg, 1);
		sg_set_page(&sg, page, PAGE_SIZE, 0);

		spin_lock_irq(&ctx->lock);
		crypto_blkcipher_set_iv(ctx->cipher->tfm, iv,
					PRIVOS_CIPHER_IV_SIZE);
#ifdef CONFIG_PRIV_CRYPT
		crypto_blkcipher_decrypt(ctx->cipher, &sg, &sg, PAGE_SIZE);
#endif
		spin_unlock_irq(&ctx->lock);
		time_end(decrypt);
	}
}

#else /* CONFIG_PRIV_SWAP */

inline void priv_swap_setup(void)
{
}

inline struct priv_swapped_page *find_priv_page(unsigned long entry)
{
	return NULL;
}
inline int add_to_priv(struct page *page)
{
	time_start(add_priv);
	time_end(add_priv);
	return 1;
}
inline void delete_all_from_priv(struct priv_cipher_ctx *ctx)
{
}
inline void delete_from_priv(unsigned long entry)
{
	time_start(delete_priv);
	time_end(delete_priv);
}
inline void delete_scratch_page(unsigned long entry)
{
}

inline void set_swapped_bit(uint64_t entry, int val)
{
}
inline int test_swapped_bit(uint64_t entry)
{
	return 0;
}

inline int priv_ctx_get(struct priv_cipher_ctx * ctx)
{
	return ctx != NULL;
}
inline void priv_ctx_put(struct priv_cipher_ctx * ctx)
{
}

inline struct page *priv_encrypt_page(struct page *page)
{
	time_start(encrypt);
	time_end(encrypt);
	return page;
}
inline void priv_decrypt_page(struct page *page)
{
	time_start(decrypt);
	time_end(decrypt);
}
#endif /* CONFIG_PRIV_SWAP */

void show_counters(void)
{
#ifdef CONFIG_PRIV_PERF
	show_counter(mm_hook);
	show_counter(encrypt);
	show_counter(decrypt);
	show_counter(add_priv);
	show_counter(delete_priv);
	show_counter(pageout);
	show_counter(locked_pass);
	show_counter(cull_pass);
	show_counter(remove_pass);
	show_counter(sync_write);
	show_counter(level_three);
	show_counter(alloc_refill);
	show_counter(alloc_node);
	show_counter(fallback_alloc);

	printk(KERN_ERR "fail_count: %lu\n", atomic64_read(&fail_count));
	printk(KERN_ERR "fallback_count: %lu\n", atomic64_read(&fallback_count));
	atomic64_set(&fail_count, 0);
	atomic64_set(&fallback_count, 0);
#endif
}

/* Totally unrelated, just scanning memory */
#ifdef CONFIG_DEATH_SCAN
#include <linux/mm.h>
#include <linux/slab.h>

#include <asm/io.h>
#define PATTERN_LEN 7
const u8 pattern[PATTERN_LEN + 1] = "\x17\x56\x13\x4a\xef\xde\xab\x00";

int match_pattern(u8 *bytes, int len)
{
        if (*bytes == 2 && memcmp(pattern, bytes + 1, PATTERN_LEN) == 0)
                return 1;
        return 0;
}

static int scan_region(u8 *bytes, unsigned long len)
{
	int count = 0;
	while (len > (PATTERN_LEN + 1)) {
		if (match_pattern(bytes, len)) {
#ifdef CONFIG_PRIV_DEBUG
			printk(KERN_ERR "Token found at %p\n", bytes);
#endif
			count ++;
			bytes += 8;
			len -= 8;
		} else {
			bytes++;
			len--;
		}
	}
	return count;
}

void scan_physical(void)
{
	void *mem = (void *)PAGE_OFFSET;
	struct sysinfo si;
	unsigned long num_scanned, addr_on, total_pages, matched = 0;
	unsigned int matched_pages = 0;

	si_meminfo(&si);

	total_pages = si.totalram;
	printk(KERN_ERR "%lu total pages", total_pages);

	/* It looks like there are breaks in the kernel physical address map.
	   Let's ensure that we've scanned a full physical memory's worth of
	   pages (which hopefully is the set of pages that we want...).  If we
	   don't do this, I've seen this miss matches. */
	num_scanned = 0;
	addr_on = 0;
	while (num_scanned < total_pages) {
		unsigned long page_start = (unsigned long)mem + (addr_on << PAGE_SHIFT);
		if (kern_addr_valid(page_start)) {
			if (matched += scan_region((u8 *)page_start, PAGE_SIZE))
				matched_pages ++;
			num_scanned++;
		}
		addr_on++;
	}
	printk(KERN_ERR "Matched %lu tokens in %u pages\n", matched, matched_pages);
	printk(KERN_ERR "Finished scan");
}
#endif
