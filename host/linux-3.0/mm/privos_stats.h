#ifndef __PRIVOS_STATS_H
#define __PRIVOS_STATS_H

#include <linux/atomic.h>

#ifdef CONFIG_PRIV_PERF
extern __inline__ uint64_t rdtsc(void);
#define def_vars(var)					\
	extern atomic64_t __perf_##var;			\
	extern atomic64_t __num_##var;
#define time_vars(var)						\
	atomic64_t __perf_##var = ATOMIC64_INIT(0);		\
	atomic64_t __num_##var  = ATOMIC64_INIT(0);
#define time_start(var) uint64_t __start_##var = rdtsc()
#define time_end(var) do {						\
		atomic64_inc(&__num_##var);				\
		atomic64_add(rdtsc() - __start_##var, &__perf_##var);	\
	} while (0)
#define ARESET(var) atomic64_set(&__perf_##var, 0), atomic64_set(&__num_##var, 0)
#define show_counter(var) do {						\
		uint64_t __perf = atomic64_read(&__perf_##var);		\
		uint64_t __num  = atomic64_read(&__num_##var);		\
		if (__perf && __num)					\
			printk(KERN_ERR ""#var ": %llu in %llu avg %llu\n", \
				__perf, __num, __perf / __num);		\
		ARESET(var);						\
	} while(0)
def_vars(mm_hook);
def_vars(add_priv);
def_vars(delete_priv);
def_vars(encrypt);
def_vars(decrypt);
def_vars(pageout);
def_vars(locked_pass);
def_vars(cull_pass);
def_vars(remove_pass);
def_vars(sync_write);

def_vars(level_three);
def_vars(alloc_refill);
def_vars(alloc_node);
def_vars(fallback_alloc);
#else
#define time_start(var) do { } while (0)
#define time_end(var) do { } while (0)
#define show_counter(var) do { } while (0)
#define time_vars(var) struct __dummy_##var { }
#endif /* CONFIG_PRIV_PERF */

extern void show_counters(void);
#endif
