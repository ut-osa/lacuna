#ifndef QEMU_PRIV_INPUT_INT_H
#define QEMU_PRIV_INPUT_INT_H

/* Code that private device code may need, but the rest of QEMU should
   not */

#include "qemu-queue.h"

#include <stdlib.h>

struct PrivDevice {
    const struct PrivDeviceOps *ops;
    QLIST_ENTRY(PrivDevice) next;
};

struct PrivDeviceOps {
    int (*connect)(struct PrivDevice *pd);
    int (*disconnect)(struct PrivDevice *pd);
    int (*free)(struct PrivDevice *pd);
};

void priv_set_kbd_enabled(int status);
void priv_set_mouse_enabled(int status);

static inline int priv_dev_connect(struct PrivDevice *pd)
{
    if (pd && pd->ops && pd->ops->connect)
        return pd->ops->connect(pd);

    return -1;
}

static inline int priv_dev_disconnect(struct PrivDevice *pd)
{
    if (pd && pd->ops && pd->ops->disconnect)
        return pd->ops->disconnect(pd);

    return -1;
}

static inline int priv_dev_free(struct PrivDevice *pd)
{
    if (pd && pd->ops && pd->ops->free)
        return pd->ops->free(pd);

    return -1;
}

#define PRIV_PROBE_NO_MATCH 0
#define PRIV_PROBE_MATCH 1

typedef int (*probe_fn_t)(const char *config, struct PrivDevice **result);

#define OPTION_TYPE "type"
#define OPTION_TOGGLER "toggler"
#define OPTION_ENCRYPT "encrypt"

/*
 * Local variables:
 * indent-tabs-mode: nil
 * c-basic-offset: 4
 * End:
 */

#endif
