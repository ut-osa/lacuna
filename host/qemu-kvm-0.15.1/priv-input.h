#ifndef QEMU_PRIV_INPUT_H
#define QEMU_PRIV_INPUT_H

#include <stdint.h>

#define PRIV_INPUT_BENCH_KEYBOARD 0x01
#define PRIV_INPUT_BENCH_MOUSE    0x02

extern uint8_t priv_input_do_bench;
extern uint32_t priv_input_bench;

int priv_config_input(const char *config);

int priv_connect_all(void);
int priv_disconnect_all(void);

int priv_free_all(void);

int priv_get_kbd_enabled(void);
int priv_get_mouse_enabled(void);

void priv_input_bench_trigger(uint32_t type);

static inline uint64_t rdtsc(void)
{
   unsigned int a, d;
   __asm__ __volatile__("rdtsc" : "=a" (a), "=d" (d));
   return ((uint64_t)a) | (((uint64_t)d) << 32);
}

/*
 * Local variables:
 * indent-tabs-mode: nil
 * c-basic-offset: 4
 * End:
 */

#endif
