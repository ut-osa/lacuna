/*
 * QEMU System Emulator
 *
 * Copyright (c) 2003-2008 Fabrice Bellard
 * Copyright (c) 2009 Red Hat, Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

/* Modification by yxu:
 * 1. tap_send: add decrypting a frame. The first 10 bytes are 0s, so start
 * from the 11th
 * 2. tap_write_packet: add encrypting a frame.
 *
 */

#include "net/tap.h"

#include "config-host.h"

#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <sys/socket.h>
#include <net/if.h>

#include "net.h"
#include "sysemu.h"
#include "qemu-char.h"
#include "qemu-common.h"
#include "qemu-error.h"

#include "net/tap-linux.h"

#include "hw/vhost_net.h"

#ifdef CONFIG_PRIV_NET
#include <priv_crypt.h>
#endif

/* Maximum GSO packet size (64k) plus plenty of room for
 * the ethernet and virtio_net headers
 */
#define TAP_BUFSIZE (4096 + 65536)

typedef struct TAPState {
    VLANClientState nc;
    int fd;
    char down_script[1024];
    char down_script_arg[128];
    uint8_t buf[TAP_BUFSIZE];
    unsigned int read_poll : 1;
    unsigned int write_poll : 1;
    unsigned int using_vnet_hdr : 1;
    unsigned int has_ufo: 1;
    VHostNetState *vhost_net;
    unsigned host_vnet_hdr_len;
} TAPState;

static int launch_script(const char *setup_script, const char *ifname, int fd);

static int tap_can_send(void *opaque);
static void tap_send(void *opaque);
static void tap_writable(void *opaque);

static void tap_update_fd_handler(TAPState *s)
{
    qemu_set_fd_handler2(s->fd,
                         s->read_poll  ? tap_can_send : NULL,
                         s->read_poll  ? tap_send     : NULL,
                         s->write_poll ? tap_writable : NULL,
                         s);
}

static void tap_read_poll(TAPState *s, int enable)
{
    s->read_poll = !!enable;
    tap_update_fd_handler(s);
}

static void tap_write_poll(TAPState *s, int enable)
{
    s->write_poll = !!enable;
    tap_update_fd_handler(s);
}

static void tap_writable(void *opaque)
{
    TAPState *s = opaque;

    tap_write_poll(s, 0);

    qemu_flush_queued_packets(&s->nc);
}

#ifdef CONFIG_PRIV_NET
/*yxu (modified by adunn)
 *
 * if addr is the same as any macaddr in nd_table, return a pointer to
 * its NICInfo
 */
static NICInfo *get_nic_with_mac(uint8_t *addr)
{
    int i = 0;
    int j = 0;
    //char *adf;
    for (; i < nb_nics; i++) {
       // adf=nd_table[i].macaddr.a;
       // printf("MAC:%02X:%02X:%02X:%02X:%02X:%02X\n", addr[0],addr[1],addr[2],addr[3],addr[4],addr[5]);
       // printf("NIC:%02X:%02X:%02X:%02X:%02X:%02X\n", adf[0],adf[1],adf[2],adf[3],adf[4],adf[5]);
        j = 0;
        for (; j < 6; j++) {
            if (nd_table[i].macaddr.a[j] != addr[j])
                break;
        }
        if (j == 6) {
            return &nd_table[i];
        }
    }
    return NULL;
}

static int priv_crypt_packet(size_t len, uint8_t *buf, int dir_in)
{
    struct priv_cctx *cctx = NULL;
    const int data_start_pos = 14, mac_len = 6;
    uint8_t *dest_mac = buf, *src_mac = buf + mac_len;

    if (dir_in) {
        NICInfo *ni = get_nic_with_mac(dest_mac);
        if (ni)
            cctx = ni->cctx_in;
    } else {
        NICInfo *ni = get_nic_with_mac(src_mac);
        if (ni)
            cctx = ni->cctx_out;
    }

    if (cctx) {
        /* yxu: need to adjust the ethernet frame type to something
           unknown since the checksum no longer matches, and thus
           would otherwise be discarded */
        const int type_byte_pos = 13;
        buf[type_byte_pos] += (dir_in ? -1 : 1);

        if (len < data_start_pos) {
            error_report("malformed packet");
            return -1;
        }

        if (priv_crypt(cctx, len - data_start_pos, buf + data_start_pos) < 0) {
            error_report("crypt failure!");
            /* Networking is in trouble here, because now the crypto
               context is in an unpredictable state. */
            return -1;
        }
    }
    return 0;
}
#else
static int priv_crypt_packet(size_t len, uint8_t *buf, int dir_in)
{
    return 0;
}
#endif

static ssize_t tap_write_packet(TAPState *s, const struct iovec *iov, int iovcnt)
{
    int i, rc;
    ssize_t len;

    //yxu: encrypt here
    for(i = 0; i < iovcnt; i++) {
        rc = priv_crypt_packet(iov[i].iov_len, iov[i].iov_base, 0);
        if (rc < 0) {
            /* Something went wrong with encrypting this frame.  Let's
               just zero it out and send it anyway.  It should be
               dropped on the network, which should be fine.  Crypto
               may be out of sync, which we can't recover from (future
               network data will be corrupted), but we might still be
               OK. */
            memset(iov[i].iov_base, 0, iov[i].iov_len);
        }
    }
    do {
        len = writev(s->fd, iov, iovcnt);
    } while (len == -1 && errno == EINTR);

    if (len == -1 && errno == EAGAIN) {
        tap_write_poll(s, 1);
        return 0;
    }

    return len;
}

static ssize_t tap_receive_iov(VLANClientState *nc, const struct iovec *iov,
                               int iovcnt)
{
    TAPState *s = DO_UPCAST(TAPState, nc, nc);
    const struct iovec *iovp = iov;
    struct iovec iov_copy[iovcnt + 1];
    struct virtio_net_hdr_mrg_rxbuf hdr = { };

    if (s->host_vnet_hdr_len && !s->using_vnet_hdr) {
        iov_copy[0].iov_base = &hdr;
        iov_copy[0].iov_len =  s->host_vnet_hdr_len;
        memcpy(&iov_copy[1], iov, iovcnt * sizeof(*iov));
        iovp = iov_copy;
        iovcnt++;
    }

    return tap_write_packet(s, iovp, iovcnt);
}

static ssize_t tap_receive_raw(VLANClientState *nc, const uint8_t *buf, size_t size)
{
    TAPState *s = DO_UPCAST(TAPState, nc, nc);
    struct iovec iov[2];
    int iovcnt = 0;
    struct virtio_net_hdr_mrg_rxbuf hdr = { };

    if (s->host_vnet_hdr_len) {
        iov[iovcnt].iov_base = &hdr;
        iov[iovcnt].iov_len  = s->host_vnet_hdr_len;
        iovcnt++;
    }

    iov[iovcnt].iov_base = (char *)buf;
    iov[iovcnt].iov_len  = size;
    iovcnt++;

    return tap_write_packet(s, iov, iovcnt);
}

static ssize_t tap_receive(VLANClientState *nc, const uint8_t *buf, size_t size)
{
    TAPState *s = DO_UPCAST(TAPState, nc, nc);
    struct iovec iov[1];

    if (s->host_vnet_hdr_len && !s->using_vnet_hdr) {
        return tap_receive_raw(nc, buf, size);
    }

    iov[0].iov_base = (char *)buf;
    iov[0].iov_len  = size;

    return tap_write_packet(s, iov, 1);
}

static int tap_can_send(void *opaque)
{
    TAPState *s = opaque;

    return qemu_can_send_packet(&s->nc);
}

#ifndef __sun__
ssize_t tap_read_packet(int tapfd, uint8_t *buf, int maxlen)
{
    return read(tapfd, buf, maxlen);
}
#endif

static void tap_send_completed(VLANClientState *nc, ssize_t len)
{
    TAPState *s = DO_UPCAST(TAPState, nc, nc);
    tap_read_poll(s, 1);
}

static void tap_send(void *opaque)
{
    TAPState *s = opaque;
    int size, rc;

    do {
        const int packet_start_pos = 10;
        uint8_t *buf = s->buf;

        size = tap_read_packet(s->fd, s->buf, sizeof(s->buf));
        if (size <= 0) {
            break;
        }
        //yxu: decrypt here
        rc = priv_crypt_packet(size - packet_start_pos, buf + packet_start_pos, 1);
        if (rc < 0) {
            /* Something went wrong decrypting, so just drop the
               packet.  Crypto might not be in sync anymore, but we
               can't recover that. */
            continue;
        }

        if (s->host_vnet_hdr_len && !s->using_vnet_hdr) {
            buf  += s->host_vnet_hdr_len;
            size -= s->host_vnet_hdr_len;
        }

        size = qemu_send_packet_async(&s->nc, buf, size, tap_send_completed);
        if (size == 0) {
            tap_read_poll(s, 0);
        }
    } while (size > 0 && qemu_can_send_packet(&s->nc));
}

int tap_has_ufo(VLANClientState *nc)
{
    TAPState *s = DO_UPCAST(TAPState, nc, nc);

    assert(nc->info->type == NET_CLIENT_TYPE_TAP);

    return s->has_ufo;
}

int tap_has_vnet_hdr(VLANClientState *nc)
{
    TAPState *s = DO_UPCAST(TAPState, nc, nc);

    assert(nc->info->type == NET_CLIENT_TYPE_TAP);

    return !!s->host_vnet_hdr_len;
}

int tap_has_vnet_hdr_len(VLANClientState *nc, int len)
{
    TAPState *s = DO_UPCAST(TAPState, nc, nc);

    assert(nc->info->type == NET_CLIENT_TYPE_TAP);

    return tap_probe_vnet_hdr_len(s->fd, len);
}

void tap_set_vnet_hdr_len(VLANClientState *nc, int len)
{
    TAPState *s = DO_UPCAST(TAPState, nc, nc);

    assert(nc->info->type == NET_CLIENT_TYPE_TAP);
    assert(len == sizeof(struct virtio_net_hdr_mrg_rxbuf) ||
           len == sizeof(struct virtio_net_hdr));

    tap_fd_set_vnet_hdr_len(s->fd, len);
    s->host_vnet_hdr_len = len;
}

void tap_using_vnet_hdr(VLANClientState *nc, int using_vnet_hdr)
{
    TAPState *s = DO_UPCAST(TAPState, nc, nc);

    using_vnet_hdr = using_vnet_hdr != 0;

    assert(nc->info->type == NET_CLIENT_TYPE_TAP);
    assert(!!s->host_vnet_hdr_len == using_vnet_hdr);

    s->using_vnet_hdr = using_vnet_hdr;
}

void tap_set_offload(VLANClientState *nc, int csum, int tso4,
                     int tso6, int ecn, int ufo)
{
    TAPState *s = DO_UPCAST(TAPState, nc, nc);
    if (s->fd < 0) {
        return;
    }

    tap_fd_set_offload(s->fd, csum, tso4, tso6, ecn, ufo);
}

static void tap_cleanup(VLANClientState *nc)
{
    TAPState *s = DO_UPCAST(TAPState, nc, nc);

    if (s->vhost_net) {
        vhost_net_cleanup(s->vhost_net);
        s->vhost_net = NULL;
    }

    qemu_purge_queued_packets(nc);

    if (s->down_script[0])
        launch_script(s->down_script, s->down_script_arg, s->fd);

    tap_read_poll(s, 0);
    tap_write_poll(s, 0);
    close(s->fd);
    s->fd = -1;
}

static void tap_poll(VLANClientState *nc, bool enable)
{
    TAPState *s = DO_UPCAST(TAPState, nc, nc);
    tap_read_poll(s, enable);
    tap_write_poll(s, enable);
}

int tap_get_fd(VLANClientState *nc)
{
    TAPState *s = DO_UPCAST(TAPState, nc, nc);
    assert(nc->info->type == NET_CLIENT_TYPE_TAP);
    return s->fd;
}

/* fd support */

static NetClientInfo net_tap_info = {
    .type = NET_CLIENT_TYPE_TAP,
    .size = sizeof(TAPState),
    .receive = tap_receive,
    .receive_raw = tap_receive_raw,
    .receive_iov = tap_receive_iov,
    .poll = tap_poll,
    .cleanup = tap_cleanup,
};

static TAPState *net_tap_fd_init(VLANState *vlan,
                                 const char *model,
                                 const char *name,
                                 int fd,
                                 int vnet_hdr)
{
    VLANClientState *nc;
    TAPState *s;

    nc = qemu_new_net_client(&net_tap_info, vlan, NULL, model, name);

    s = DO_UPCAST(TAPState, nc, nc);

    s->fd = fd;
    s->host_vnet_hdr_len = vnet_hdr ? sizeof(struct virtio_net_hdr) : 0;
    s->using_vnet_hdr = 0;
    s->has_ufo = tap_probe_has_ufo(s->fd);
    tap_set_offload(&s->nc, 0, 0, 0, 0, 0);
    tap_read_poll(s, 1);
    s->vhost_net = NULL;
    return s;
}

static int launch_script(const char *setup_script, const char *ifname, int fd)
{
    sigset_t oldmask, mask;
    int pid, status;
    char *args[3];
    char **parg;

    sigemptyset(&mask);
    sigaddset(&mask, SIGCHLD);
    sigprocmask(SIG_BLOCK, &mask, &oldmask);

    /* try to launch network script */
    pid = fork();
    if (pid == 0) {
        int open_max = sysconf(_SC_OPEN_MAX), i;

        for (i = 0; i < open_max; i++) {
            if (i != STDIN_FILENO &&
                i != STDOUT_FILENO &&
                i != STDERR_FILENO &&
                i != fd) {
                close(i);
            }
        }
        parg = args;
        *parg++ = (char *)setup_script;
        *parg++ = (char *)ifname;
        *parg = NULL;
        execv(setup_script, args);
        _exit(1);
    } else if (pid > 0) {
        while (waitpid(pid, &status, 0) != pid) {
            /* loop */
        }
        sigprocmask(SIG_SETMASK, &oldmask, NULL);

        if (WIFEXITED(status) && WEXITSTATUS(status) == 0) {
            return 0;
        }
    }
    fprintf(stderr, "%s: could not launch network script\n", setup_script);
    return -1;
}

static int net_tap_init(QemuOpts *opts, int *vnet_hdr)
{
    int fd, vnet_hdr_required;
    char ifname[128] = {0,};
    const char *setup_script;

    if (qemu_opt_get(opts, "ifname")) {
        pstrcpy(ifname, sizeof(ifname), qemu_opt_get(opts, "ifname"));
    }

    *vnet_hdr = qemu_opt_get_bool(opts, "vnet_hdr", 1);
    if (qemu_opt_get(opts, "vnet_hdr")) {
        vnet_hdr_required = *vnet_hdr;
    } else {
        vnet_hdr_required = 0;
    }

    TFR(fd = tap_open(ifname, sizeof(ifname), vnet_hdr, vnet_hdr_required));
    if (fd < 0) {
        return -1;
    }

    setup_script = qemu_opt_get(opts, "script");
    if (setup_script &&
        setup_script[0] != '\0' &&
        strcmp(setup_script, "no") != 0 &&
        launch_script(setup_script, ifname, fd)) {
        close(fd);
        return -1;
    }

    qemu_opt_set(opts, "ifname", ifname);

    return fd;
}

int net_init_tap(QemuOpts *opts, Monitor *mon, const char *name, VLANState *vlan)
{
    TAPState *s;
    int fd, vnet_hdr = 0;

    if (qemu_opt_get(opts, "fd")) {
        if (qemu_opt_get(opts, "ifname") ||
            qemu_opt_get(opts, "script") ||
            qemu_opt_get(opts, "downscript") ||
            qemu_opt_get(opts, "vnet_hdr")) {
            error_report("ifname=, script=, downscript= and vnet_hdr= is invalid with fd=");
            return -1;
        }

        fd = net_handle_fd_param(mon, qemu_opt_get(opts, "fd"));
        if (fd == -1) {
            return -1;
        }

        fcntl(fd, F_SETFL, O_NONBLOCK);

        vnet_hdr = tap_probe_vnet_hdr(fd);
    } else {
        if (!qemu_opt_get(opts, "script")) {
            qemu_opt_set(opts, "script", DEFAULT_NETWORK_SCRIPT);
        }

        if (!qemu_opt_get(opts, "downscript")) {
            qemu_opt_set(opts, "downscript", DEFAULT_NETWORK_DOWN_SCRIPT);
        }

        fd = net_tap_init(opts, &vnet_hdr);
        if (fd == -1) {
            return -1;
        }
    }

    s = net_tap_fd_init(vlan, "tap", name, fd, vnet_hdr);
    if (!s) {
        close(fd);
        return -1;
    }

    if (tap_set_sndbuf(s->fd, opts) < 0) {
        return -1;
    }

    if (qemu_opt_get(opts, "fd")) {
        snprintf(s->nc.info_str, sizeof(s->nc.info_str), "fd=%d", fd);
    } else {
        const char *ifname, *script, *downscript;

        ifname     = qemu_opt_get(opts, "ifname");
        script     = qemu_opt_get(opts, "script");
        downscript = qemu_opt_get(opts, "downscript");

        snprintf(s->nc.info_str, sizeof(s->nc.info_str),
                 "ifname=%s,script=%s,downscript=%s",
                 ifname, script, downscript);

        if (strcmp(downscript, "no") != 0) {
            snprintf(s->down_script, sizeof(s->down_script), "%s", downscript);
            snprintf(s->down_script_arg, sizeof(s->down_script_arg), "%s", ifname);
        }
    }

    if (qemu_opt_get_bool(opts, "vhost", !!qemu_opt_get(opts, "vhostfd") ||
                          qemu_opt_get_bool(opts, "vhostforce", false))) {
        int vhostfd, r;
        bool force = qemu_opt_get_bool(opts, "vhostforce", false);
        if (qemu_opt_get(opts, "vhostfd")) {
            r = net_handle_fd_param(mon, qemu_opt_get(opts, "vhostfd"));
            if (r == -1) {
                return -1;
            }
            vhostfd = r;
        } else {
            vhostfd = -1;
        }
        s->vhost_net = vhost_net_init(&s->nc, vhostfd, force);
        if (!s->vhost_net) {
            error_report("vhost-net requested but could not be initialized");
            return -1;
        }
    } else if (qemu_opt_get(opts, "vhostfd")) {
        error_report("vhostfd= is not valid without vhost");
        return -1;
    }

    return 0;
}

VHostNetState *tap_get_vhost_net(VLANClientState *nc)
{
    TAPState *s = DO_UPCAST(TAPState, nc, nc);
    assert(nc->info->type == NET_CLIENT_TYPE_TAP);
    return s->vhost_net;
}

/*
 * Local variables:
 * indent-tabs-mode: nil
 * c-basic-offset: 4
 * End:
 */
