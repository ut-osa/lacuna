#include "sysemu.h"
#include "priv-input.h"

#include "priv-input-int.h"
#include "priv-usb.h"
#include "priv-util.h"
#include "priv-pci.h"

#include <stdio.h>
#include <stdint.h>

#include "hw/priv-hotplug.h"

//#define DEBUG 1

#define ERROR(fmt, args...) fprintf(stderr, fmt, ##args)

#ifdef DEBUG
#define DPRINTF(fmt, args...) fprintf(stderr, fmt, ##args)
#else
#define DPRINTF(fmt, args...) do { } while (0);
#endif

/* Whether to do private input switch time benchmarking */
uint8_t priv_input_do_bench = 0;
/* Which devices to benchmark for switch time */
uint32_t priv_input_bench = 0;
/* Which devices have registered as being present so far for
   benchmarking.  When all the desired ones have, the switch is
   complete. */
uint32_t priv_input_bench_status = 0;
static Notifier exit_notifier_var;
static QLIST_HEAD(, PrivDevice) priv_devices =
    QLIST_HEAD_INITIALIZER(priv_devices);

probe_fn_t probe_fns[] = {
    priv_config_usb,
    priv_config_pci,
    NULL
};

static void exit_notifier(struct Notifier *n, void *data);

static void setup_exit_notifier(void)
{
    static int is_setup = 0;

    if (!is_setup) {
        DPRINTF("%s\n", __func__);

        is_setup = 1;
        exit_notifier_var.notify = exit_notifier;
        qemu_add_exit_notifier(&exit_notifier_var);
    }
}

int priv_config_input(const char *config)
{
    struct PrivDevice *pd;
    probe_fn_t *pf;

    /* This seems as good a place as any to set up the exit
       notifier */
    setup_exit_notifier();

    for (pf = probe_fns; *pf != NULL; pf++) {
        pd = NULL;
        if ((*pf)(config, &pd) == PRIV_PROBE_MATCH) {
            if (pd == NULL)
                return -1;

            QLIST_INSERT_HEAD(&priv_devices, pd, next);

            return 0;
        }
    }

    /* No match */
    ERROR("No match for priv-input device with config '%s'\n", config);
    return -1;
}

static int priv_dev_for_all(int (*fn)(struct PrivDevice *pd))
{
    struct PrivDevice *pd;

    QLIST_FOREACH(pd, &priv_devices, next) {
        if (fn(pd) < 0)
            return -1;
    }

    return 0;
}

int priv_connect_all(void)
{
    int rc;

    DPRINTF("%s\n", __func__);

    set_hotplug_needs_reset(1);

    if (priv_input_do_bench) {
        printf("%s start: %lu\n", __func__, rdtsc());
    }

    rc = priv_dev_for_all(priv_dev_connect);

    if (priv_input_do_bench) {
        printf("%s end: %lu\n", __func__, rdtsc());
    }

    return rc;
}

int priv_disconnect_all(void)
{
    DPRINTF("%s\n", __func__);

    set_hotplug_needs_reset(1);

    return priv_dev_for_all(priv_dev_disconnect);
}

int priv_free_all(void)
{
    DPRINTF("%s\n", __func__);

    /* XXX: Should tear down the list too */
    return priv_dev_for_all(priv_dev_free);
}

static void exit_notifier(struct Notifier *n, void *data)
{
    priv_free_all();
}

static int priv_kbd_is_enabled = 0;
int priv_get_kbd_enabled(void)
{
    return priv_kbd_is_enabled;
}

void priv_set_kbd_enabled(int status)
{
    DPRINTF("%s: %d\n", __func__, status);

    if (status)
        priv_input_bench |= PRIV_INPUT_BENCH_KEYBOARD;
    else
        priv_input_bench &= ~PRIV_INPUT_BENCH_KEYBOARD;

    priv_kbd_is_enabled = status;
}

static int priv_mouse_is_enabled = 0;
int priv_get_mouse_enabled(void)
{
    return priv_mouse_is_enabled;
}

void priv_set_mouse_enabled(int status)
{
    DPRINTF("%s: %d\n", __func__, status);

    if (status)
        priv_input_bench |= PRIV_INPUT_BENCH_MOUSE;
    else
        priv_input_bench &= ~PRIV_INPUT_BENCH_MOUSE;

    priv_mouse_is_enabled = status;
}

void priv_input_bench_trigger(uint32_t type)
{
    priv_input_bench_status |= type;

    DPRINTF("%s debug: %u\n", __func__, type);

    if (priv_input_do_bench &&
        ((priv_input_bench_status & priv_input_bench) == priv_input_bench)) {
        printf("%s: %lu\n", __func__, rdtsc());
        exit(1);
    }
}

/*
 * Local variables:
 * indent-tabs-mode: nil
 * c-basic-offset: 4
 * End:
 */
