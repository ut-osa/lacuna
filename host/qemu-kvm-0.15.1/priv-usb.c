#include <priv_crypt.h>

#include "priv-usb.h"
#include "priv-util.h"

#include "qemu-common.h"
#include "qemu-option.h"

#include "hw/usb.h"

//#define DEBUG 1

#define ERROR(fmt, args...) fprintf(stderr, fmt, ##args)

#ifdef DEBUG
#define DPRINTF(fmt, args...) fprintf(stderr, fmt, ##args)
#else
#define DPRINTF(fmt, args...) do { } while (0);
#endif

#define OPTION_BUS_NUM "bus_num"

#define USB_KBD_PACKET_LEN 8
#define USB_KBD_LCTRL 0x01
#define USB_KBD_LALT  0x04

struct priv_kbd_info {
    uint8_t lctrl, lalt;
};

struct PrivUSBDevice {
    struct PrivDevice pd;
    USBDevice *dev;

    void *opaque;
    int toggler;

    struct priv_cctx *cctx;
    priv_crypt_id_t cctx_id;
    int encrypt;

    unsigned int bus, addr;
    char bus_num[256];
};

/* sdl_grab_end is used to exit private mode */
extern void sdl_grab_end(void);

static void print_packet_len_warning(void)
{
    static int printed = 0;

    if (!printed) {
        fprintf(stderr, "Packet length != %d received from keyboard, packets may not be of expected format", USB_KBD_PACKET_LEN);
        printed = 1;
    }
}

static int priv_handle_enc_packet(USBDevice *dev, USBPacket *packet)
{
    struct PrivUSBDevice *pud = dev->priv_opaque;

#ifdef DEBUG
    print_hex(packet->data, packet->len);
    DPRINTF("->\n");
#endif

    if (priv_crypt(pud->cctx, packet->len, packet->data) < 0) {
        ERROR("%s: decryption error!\n", __func__);
        /* XXX: return 1 so that packet doesn't complete? */
        return 0;
    }

#ifdef DEBUG
    print_hex(packet->data, packet->len);
#endif

    return 0;
}

/* Check for control and alt state changes based upon what it commonly
 * seems USB keyboards use for their report formats.
 *
 * This is a hack, and but it seems to work for every USB keyboard I
 * have used. */
static int priv_kbd_handle_packet(USBDevice *dev, USBPacket *packet)
{
    struct PrivUSBDevice *pud = dev->priv_opaque;
    struct priv_kbd_info *pki = pud->opaque;
    uint8_t ctrl_alt_state, on;

    if (pud->encrypt)
        priv_handle_enc_packet(dev, packet);

    if (packet->len != USB_KBD_PACKET_LEN)
        print_packet_len_warning();

    if (packet->len < 1)
        return 0;

    on = 0;
    ctrl_alt_state = packet->data[0];

    if ((ctrl_alt_state & USB_KBD_LCTRL) && !pki->lctrl) {
        DPRINTF("priv-input: Left ctrl pressed!\n");

        on = 1;
    }
    if ((ctrl_alt_state & USB_KBD_LALT) && !pki->lalt) {
        DPRINTF("priv-input: Left alt pressed!\n");

        on = 1;
    }

    pki->lctrl = !!(ctrl_alt_state & USB_KBD_LCTRL);
    pki->lalt = !!(ctrl_alt_state & USB_KBD_LALT);

    /* If control or alt just turned on, and now control and alt are
       both on, then release input grab if grabbed */
    if (on && pki->lctrl && pki->lalt) {
        DPRINTF("priv-input: Release input grab!\n");

        sdl_grab_end();
        
        /* 1 indicates that this packet is handled, since we're deleting it */
        return 1;
    }

    return 0;
}

extern int usb_linux_get_port(char *port,
                              uint8_t bus, uint8_t addr);

static int get_dev_name(char *buf,
                        uint8_t bus, uint8_t addr)
{
    char port[16];
    int configuration = 1, interface = 0;

    if (usb_linux_get_port(port, bus, addr) < 0)
        return -1;

    /* XXX: configuration and interface should be found from the
       actual device settings */
    sprintf(buf, "%d-%s:%d.%d", bus, port, configuration, interface);

    return 0;
}

/* NOTE: Can't necessarily rely on pud->dev here! */
static int usb_crypt_setup(struct PrivUSBDevice *pud)
{
    int rc = -1, fd = -1;
    char device_name[64];
    char sysfs_buf[16];
    size_t sysfs_buf_sz;
    char crypt_ctx_id_path[PATH_MAX];
    priv_crypt_id_t cctx_id;

    if ((rc = priv_create_cctx(&pud->cctx, PRIV_CRYPT_ALG_AES_CTR, 16)) < 0)
        goto out;

    if ((rc = priv_new_backend(pud->cctx)) < 0)
        goto out_destroy;

    if ((rc = priv_get_id(pud->cctx, &cctx_id)) < 0)
        goto out_destroy;

    if ((rc = get_dev_name(device_name,  pud->bus, pud->addr)) < 0)
        goto out_destroy;

    /* use sysfs file to set USB device to be able to encrypt */
    /* XXX: endpoint should be found from the actual device settings,
       might take some user configuration? */
    snprintf(crypt_ctx_id_path, sizeof(crypt_ctx_id_path),
             "/sys/bus/usb/devices/%s/ep_81/crypt_ctx_id",
             device_name);
    sysfs_buf_sz = snprintf(sysfs_buf, sizeof(sysfs_buf), "%d", cctx_id);

    DPRINTF("%s: crypt_ctx_id_path is %s\n",
            __func__, crypt_ctx_id_path);

    fd = open(crypt_ctx_id_path, O_RDWR);
    if (fd < 0) {
        ERROR("%s: couldn't open %s\n", __func__, crypt_ctx_id_path);
        rc = -1;
        goto out_destroy;
    }

    if (write(fd, sysfs_buf, sysfs_buf_sz) != sysfs_buf_sz)
        goto out_destroy;

    rc = 0;
    goto out;

 out_destroy:
    priv_destroy_cctx(pud->cctx);
 out:
    if (fd >= 0)
        close(fd);
    return rc;
}

static int usb_dev_connect(struct PrivDevice *pd)
{
    int rc = -1;
    struct PrivUSBDevice *pud = container_of(pd, struct PrivUSBDevice, pd);

    if (!(pud->dev = usb_host_device_open(pud->bus_num))) {
        ERROR("%s: add failed (%s)\n", __func__, pud->bus_num);
        goto out;
    } else {
        DPRINTF("%s: add succeeded (%s)\n", __func__, pud->bus_num);
        rc = 0;
    }

    if (pud->encrypt) {
        if (priv_activate(pud->cctx) < 0) {
            ERROR("%s: activate failed (%s)\n", __func__, pud->bus_num);
            rc = -1;
            goto out;
        }
        pud->dev->priv_handle_packet = priv_handle_enc_packet;
        pud->dev->priv_opaque = pud;
    }

 out:
    return rc;
}

static int usb_kbd_connect(struct PrivDevice *pd)
{
    int rc = usb_dev_connect(pd);
    struct PrivUSBDevice *pud;

    if (rc < 0)
        return rc;

    pud = container_of(pd, struct PrivUSBDevice, pd);

    if (pud->toggler) {
        DPRINTF("%s: making '%s' a toggler\n", __func__, pud->bus_num);

        pud->opaque = qemu_mallocz(sizeof(struct priv_kbd_info));

        /* Replace encrypted packet handler with one that also does
           some keyboard state tracking */
        pud->dev->priv_handle_packet = priv_kbd_handle_packet;
        pud->dev->priv_opaque = pud;
    }

    return rc;
}

static int usb_dev_disconnect(struct PrivDevice *pd)
{
    int rc = -1;
    struct PrivUSBDevice *pud = container_of(pd, struct PrivUSBDevice, pd);

    /* Check if already disconnected */
    if (!pud->dev)
        return 0;

    if (pud->encrypt) {
        if (priv_deactivate(pud->cctx) < 0)
            ERROR("%s: deactivate failed (%s)\n", __func__, pud->bus_num);        
    }

    pud->dev->priv_handle_packet = NULL;
    pud->dev->priv_opaque = NULL;

    if (usb_close_host_dev(pud->dev) < 0) {
        ERROR("%s: del failed\n", __func__);
    } else {
        DPRINTF("%s: del succeeded\n", __func__);
        rc = 0;
    }

    return rc;
}

static int usb_kbd_disconnect(struct PrivDevice *pd)
{
    struct PrivUSBDevice *pud;

    pud = container_of(pd, struct PrivUSBDevice, pd);
    if (pud->toggler) {
        qemu_free(pud->opaque);
        pud->opaque = NULL;
    }

    return usb_dev_disconnect(pd);
}

static int usb_dev_free(struct PrivDevice *pd)
{
    struct PrivUSBDevice *pud = container_of(pd, struct PrivUSBDevice, pd);

    if (pud->cctx)
        priv_destroy_cctx(pud->cctx);

    /* XXX: To really free all resources associated with this device,
       more cleanup needs to be done.  However, in practice, this only
       happens when QEMU is quitting, so it won't matter. */

    return 0;
}

const struct PrivDeviceOps usb_kbd_ops = {
    .connect = usb_kbd_connect,
    .disconnect = usb_kbd_disconnect,
    .free = usb_dev_free,
};

const struct PrivDeviceOps usb_mouse_ops = {
    .connect = usb_dev_connect,
    .disconnect = usb_dev_disconnect,
    .free = usb_dev_free,
};

static QEMUOptionParameter priv_usb_options[] = {
    {
        .name = OPTION_TYPE,
        .type = OPT_STRING
    },
    {
        .name = OPTION_TOGGLER,
        .type = OPT_FLAG
    },
    {
        .name = OPTION_BUS_NUM,
        .type = OPT_STRING
    },
    {
        .name = OPTION_ENCRYPT,
        .type = OPT_FLAG
    },
    { NULL },
};

int priv_config_usb(const char *config, struct PrivDevice **pd)
{
    int rc = PRIV_PROBE_NO_MATCH;
    QEMUOptionParameter *parsed_params = NULL, *param;
    struct PrivUSBDevice *result;
    const struct PrivDeviceOps *ops;

    if (!config || !pd)
        return -1;

    *pd = NULL;

    if (!strstr(config, OPTION_TYPE "=usb"))
        goto out;

    /* We may still encounter an error in device setup, but this is
       the handler that should match the config */
    rc = PRIV_PROBE_MATCH;

    parsed_params = parse_option_parameters(config, priv_usb_options, NULL);
    if (!parsed_params)
        goto out;

    param = get_option_parameter(parsed_params, OPTION_TYPE);
    if (!param)
        goto out;

    if (!strcmp(param->value.s, "usb-kbd")) {
        ops = &usb_kbd_ops;
    } else if (!strcmp(param->value.s, "usb-mouse")) {
        ops = &usb_mouse_ops;
    } else {
        goto out;
    }

    result = qemu_mallocz(sizeof(struct PrivUSBDevice));

    param = get_option_parameter(parsed_params, OPTION_BUS_NUM);
    if (!param)
        goto free;

    sstrncpy(result->bus_num, param->value.s, sizeof(result->bus_num));
    if (sscanf(result->bus_num, "%u.%u", &result->bus, &result->addr) != 2)
        goto free;

    param = get_option_parameter(parsed_params, OPTION_TOGGLER);
    if (param)
        result->toggler = !!param->value.n;

    param = get_option_parameter(parsed_params, OPTION_ENCRYPT);
    if (param) {
        result->encrypt = !!param->value.n;
    }

    result->pd.ops = ops;

    if (result->encrypt) {
        if (usb_crypt_setup(result) < 0)
            goto free;
    }
    if (ops == &usb_kbd_ops) {
        priv_set_kbd_enabled(1);
    } else if (ops == &usb_mouse_ops) {
        priv_set_mouse_enabled(1);
    }

    *pd = &result->pd;

    goto out;

 free:
    qemu_free(result);

 out:
    if (parsed_params)
        free_option_parameters(parsed_params);

    return rc;
}

/*
 * Local variables:
 * indent-tabs-mode: nil
 * c-basic-offset: 4
 * End:
 */
