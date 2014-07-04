#ifndef QEMU_PRIV_PCI_H
#define QEMU_PRIV_PCI_H

#include "priv-input-int.h"

int priv_config_pci(const char *config, struct PrivDevice **pd);

/*
 * Local variables:
 * indent-tabs-mode: nil
 * c-basic-offset: 4
 * End:
 */

#endif
