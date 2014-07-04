#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>

#include "qemu-option.h"
#include "qemu-config.h"
#include "hw/qdev.h"

#include "priv-pci.h"
#include "priv-util.h"

#define OPTION_BUS_NUM "bus_num"
#define OPTION_BIND_SCRIPT "bind-script"
#define OPTION_UNBIND_SCRIPT "unbind-script"

#define QDEV_ID_PREFIX "priv"

#define ERROR(fmt, args...) fprintf(stderr, fmt, ##args)

#define DEBUG 1

#ifdef DEBUG
#define DPRINTF(fmt, args...) fprintf(stderr, fmt, ##args)
#else
#define DPRINTF(fmt, args...) do { } while (0);
#endif

const int use_scripts = 0;

struct PrivPCIUSBBus {
    struct PrivDevice pd;

    /* XXX: Accommodate other domains? */
    int bus, dev, fn;
    int qdev_id;
    char driver_name[32];
    char *bind_script, *unbind_script;
};

static QEMUOptionParameter priv_pci_usb_bus_options[] = {
    {
        .name = OPTION_TYPE,
        .type = OPT_STRING
    },
    {
        .name = OPTION_BUS_NUM,
        .type = OPT_STRING
    },
    {
        .name = OPTION_BIND_SCRIPT,
        .type = OPT_STRING
    },
    {
        .name = OPTION_UNBIND_SCRIPT,
        .type = OPT_STRING
    },
    { NULL },
};

static int get_new_qdev_id(void)
{
    static int qdev_id=0;

    /* XXX: need locking? with SMP>1, what happens to QEMU? */

    qdev_id++;
    return qdev_id;
}

static int launch_script(const char *script_path, char *args[])
{
    /* Mainly copying launch_script from net/tap.c. Could merge the
       two if willing to test that we didn't break anything. */
    sigset_t oldmask, mask;
    int pid, status;
    char **arg;

    if (!script_path || !args)
        return -1;

    DPRINTF("%s: ", __func__);
    for (arg = args; *arg != NULL; arg++) {
        DPRINTF("%s ", *arg);
    }
    DPRINTF("\n");

    /* Not 100% sure this is necessary given that ignore is the
       default action for SIGCHLD */
    sigemptyset(&mask);
    sigaddset(&mask, SIGCHLD);
    sigprocmask(SIG_BLOCK, &mask, &oldmask);

    pid = fork();
    if (pid == 0) {
        int open_max = sysconf(_SC_OPEN_MAX), i;

        for (i = 0; i < open_max; i++) {
            if (i != STDIN_FILENO &&
                i != STDOUT_FILENO &&
                i != STDERR_FILENO) {
                close(i);
            }
        }

        execv(script_path, args);
        _exit(1);
    } else if (pid > 0) {
        while (waitpid(pid, &status, 0) != pid) {
            /* loop (why?) */
        }
        sigprocmask(SIG_SETMASK, &oldmask, NULL);

        if (WIFEXITED(status) && WEXITSTATUS(status) == 0) {
            return 0;
        }
        DPRINTF("exit status %d\n", WEXITSTATUS(status));
        return -1;
    }
    ERROR("Could not launch script %s\n", script_path);
    return -1;
}

static int find_usb_driver_name(struct PrivPCIUSBBus *pb)
{
    char sysfs_loc[PATH_MAX], sysfs_result[PATH_MAX];
    char *driver_name;

    if (!pb)
        return -1;

    /* Look at sysfs to figure out driver for a PCI USB controller */

    snprintf(sysfs_loc, sizeof(sysfs_loc),
             "/sys/bus/pci/devices/0000:%02x:%02x.%1x/driver",
             pb->bus, pb->dev, pb->fn);

    /* readlink doesn't append a NUL terminator */
    memset(sysfs_result, 0, sizeof(sysfs_result));
    if (readlink(sysfs_loc, sysfs_result, sizeof(sysfs_result) - 1) < 0)
        return -1;

    if ((driver_name = strrchr(sysfs_result, '/')) == NULL)
        return -1;

    driver_name++;

    sstrncpy(pb->driver_name, driver_name, sizeof(pb->driver_name));
    DPRINTF("%s: driver name: '%s'\n", __func__, pb->driver_name);

    return 0;
}

static int do_nothing(struct PrivDevice *pd) {
    return 0;
}

static int disconnect_pci_with_script(struct PrivPCIUSBBus *pb)
{
    char *argv[3];
    char bus_num[16];
    argv[0] = pb->unbind_script;
    argv[1] = bus_num;
    argv[2] = (char)0;

    snprintf(bus_num, sizeof(bus_num),
             "0000:%02x:%02x.%1x", pb->bus, pb->dev, pb->fn);

    if (launch_script(argv[0], argv) < 0) {
        ERROR("unbind script failed\n");
        return -1;
    }
    
    return 0;
}

/* Bind or unbind a PCI USB controller from its driver */
static int sysfs_pci_change_connect(struct PrivPCIUSBBus *pb, int connect)
{
    int sz, fd;
    char bus_num[16], sysfs_path[PATH_MAX];

    snprintf(sysfs_path, sizeof(sysfs_path),
             "/sys/bus/pci/drivers/%s/%sbind",
             pb->driver_name, connect ? "" : "un");

    sz = snprintf(bus_num, sizeof(bus_num), "0000:%02x:%02x.%1x",
                  pb->bus, pb->dev, pb->fn);

    if ((fd = open(sysfs_path, O_WRONLY)) < 0)
        return -1;

    if (write(fd, bus_num, sz) != sz)
        return -1;

    close(fd);

    return 0;
}

static int disconnect_pci_manually(struct PrivPCIUSBBus *pb)
{
    return sysfs_pci_change_connect(pb, 0);
}

static int disconnect_pci(struct PrivPCIUSBBus *pb)
{
    /* We provide the ability to perform PCI manipulation with scripts
       or with C code.  Scripts were good for learning how to perform
       the proper actions and are good for debugging.  However, we
       don't use scripts right now through the code. */
    if (use_scripts)
        return disconnect_pci_with_script(pb);
    else
        return disconnect_pci_manually(pb);
}

static int pci_usb_bus_connect(struct PrivDevice *pd) {
    /* Unbind the USB controller from its original driver, then add
       pci device to QEMU */

    struct PrivPCIUSBBus *pb = container_of(pd, struct PrivPCIUSBBus, pd);
    QemuOptsList *opts_list;
    QemuOpts *opts;
    char opts_buf[1024];

    if (disconnect_pci(pb) < 0) {
        ERROR("disconnect from host failed\n");
        return -1;
    }

    /* Add device to QEMU hardware model */
    /* Emulating how I see the QEMU monitor working, there may be an
       easier way... */

    opts_list = qemu_find_opts("device");
    if (!opts_list) {
        ERROR("couldn't find device options list?\n");
        return -1;
    }

    snprintf(opts_buf, sizeof(opts_buf),
             "driver=pci-assign,host=%02x:%02x.%1x,id=%s%d", 
             pb->bus, pb->dev, pb->fn,
             QDEV_ID_PREFIX, pb->qdev_id);

    DPRINTF("opts_buf: '%s'\n", opts_buf);

    /* XXX: do we need to delete the opts when we're done? */
    opts = qemu_opts_parse(opts_list, opts_buf, 0);
    if (!opts) {
        ERROR("couldn't get options to parse?\n");
        return -1;
    }

    if (!qdev_device_add(opts)) {
        ERROR("qdev_device_add returned -1\n");
        return -1;
    }

    return 0;
}

static int reconnect_pci_with_script(struct PrivPCIUSBBus *pb)
{
    char bus_num[16];
    char *argv[4];

    argv[0] = pb->bind_script;
    argv[1] = bus_num;
    argv[2] = pb->driver_name;
    argv[3] = (char)0;

    snprintf(bus_num, sizeof(bus_num), "0000:%02x:%02x.%1x",
             pb->bus, pb->dev, pb->fn);

    if (launch_script(argv[0], argv) < 0) {
        ERROR("unbind script failed\n");
        return -1;
    }

    return 0;
}

static int reconnect_pci_manually(struct PrivPCIUSBBus *pb)
{
    return sysfs_pci_change_connect(pb, 1);
}

static int reconnect_pci(struct PrivPCIUSBBus *pb)
{
    if (use_scripts)
        return reconnect_pci_with_script(pb);
    else
        return reconnect_pci_manually(pb);
}

static int pci_usb_bus_disconnect(struct PrivDevice *pd) {
    /* Rebind the USB controller to its original driver (that we
       already recorded) */

    char qdev_id[16];
    DeviceState *qdev = NULL;
    struct PrivPCIUSBBus *pb = container_of(pd, struct PrivPCIUSBBus, pd);

    DPRINTF("%s\n", __func__);

    /* Remove device from QEMU hardware model */
    snprintf(qdev_id, sizeof(qdev_id), QDEV_ID_PREFIX "%d", pb->qdev_id);

    DPRINTF("%s: del %s\n", __func__, qdev_id);

    /* It's not enough to "qdev_unplug" the device, because this
       merely signals the OS to later trigger an eject (which leads to
       qdev_free).  We need it to die now.  It seems you then need to
       both qdev_unplug AND qdev_free it, the former because otherwise
       the guest OS gets confused. */
    if ((qdev = qdev_find_by_id(qdev_id)) == NULL)
        return -1;

    if (qdev_unplug(qdev) < 0)
        return -1;

    qdev_free(qdev);

    if (reconnect_pci(pb) < 0) {
        ERROR("restore to host failed\n");
        return -1;
    }

    return 0;
}

/* XXX: Use a real free operation... */
const struct PrivDeviceOps pci_usb_bus_ops = {
    .connect = pci_usb_bus_connect,
    .disconnect = pci_usb_bus_disconnect,
    .free = do_nothing,
};

static void config_pci_usb_bus(const char *config, struct PrivDevice **pd)
{
    int bus, dev, fn;
    QEMUOptionParameter *parsed_params = NULL, *param;
    struct PrivPCIUSBBus *result = NULL;
    char *bind_script = NULL, *unbind_script = NULL;

    parsed_params = parse_option_parameters(config, priv_pci_usb_bus_options, NULL);
    if (!parsed_params)
        goto out;

    param = get_option_parameter(parsed_params, OPTION_BUS_NUM);
    if (!param)
        goto out;

    if (sscanf(param->value.s, "%x:%x.%x", &bus, &dev, &fn) != 3) {
        ERROR("Invalid bus number '%s'\n", param->value.s);
        goto out;
    }

    param = get_option_parameter(parsed_params, OPTION_BIND_SCRIPT);
    if (!param) {
        ERROR("Missing option %s\n", OPTION_BIND_SCRIPT);
        goto out;
    }
    bind_script = qemu_strdup(param->value.s);

    param = get_option_parameter(parsed_params, OPTION_UNBIND_SCRIPT);
    if (!param) {
        ERROR("Missing option %s\n", OPTION_UNBIND_SCRIPT);
        goto free;
    }
    unbind_script = qemu_strdup(param->value.s);

    DPRINTF("%s: bus = %x, device = %x, fn = %x\n", __func__, bus, dev, fn);

    result = qemu_mallocz(sizeof(struct PrivPCIUSBBus));
    result->bus = bus; result->dev = dev; result->fn = fn;
    result->bind_script = bind_script; result->unbind_script = unbind_script;
    result->qdev_id = get_new_qdev_id();

    /* Find driver name for USB controller to allow later rebind */
    if (find_usb_driver_name(result) < 0) {
        ERROR("Couldn't find driver name for USB bus\n");
        goto free;
    }

    result->pd.ops = &pci_usb_bus_ops;

    *pd = &result->pd;
    goto out;

 free:
    if (result)
        free(result);
    if (bind_script)
        qemu_free(bind_script);
    if (unbind_script)
        qemu_free(unbind_script);

 out:
    if (parsed_params)
        free_option_parameters(parsed_params);

    return;
}

#if 0
/* As of now, unused */
static QEMUOptionParameter priv_pci_usb_dev_options[] = {
    {
        .name = OPTION_TYPE,
        .type = OPT_STRING
    },
    {
        .name = OPTION_TOGGLER,
        .type = OPT_FLAG
    },
    { NULL },
};
#endif

const struct PrivDeviceOps do_nothing_ops = {
    .connect = do_nothing,
    .disconnect = do_nothing,
    .free = do_nothing,
};

static void config_pci_usb_dev(const char *config, struct PrivDevice **pd, int kbd)
{
    struct PrivDevice *result = NULL;

    /* XXX: For now, just make devices that don't do anything, use
       this device as a placeholder to indicate that there is a USB
       kbd/mouse.  Also, assuming toggler. */

    result = qemu_mallocz(sizeof(struct PrivDevice));
    result->ops = &do_nothing_ops;

    /* For now, just keyboard and mouse */
    if (kbd) {
        priv_set_kbd_enabled(1);
    } else {
        priv_set_mouse_enabled(1);
    }

    *pd = result;
}

int priv_config_pci(const char *config, struct PrivDevice **pd)
{
    if (!config || !pd)
        return -1;

    if (!strstr(config, OPTION_TYPE "=pci"))
        return PRIV_PROBE_NO_MATCH;

    *pd = NULL;

    /* We may still have an error, but this is the handler that should
       match the config if possible */

    if (strstr(config, OPTION_TYPE "=pci-usb-bus"))
        config_pci_usb_bus(config, pd);
    else if (strstr(config, OPTION_TYPE "=pci-usb-kbd"))
        config_pci_usb_dev(config, pd, 1);
    else if (strstr(config, OPTION_TYPE "=pci-usb-mouse"))
        config_pci_usb_dev(config, pd, 0);

    return PRIV_PROBE_MATCH;
}

/*
 * Local variables:
 * indent-tabs-mode: nil
 * c-basic-offset: 4
 * End:
 */
