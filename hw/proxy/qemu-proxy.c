/*
 * Copyright 2019, Oracle and/or its affiliates.
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

#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <unistd.h>
#include <assert.h>
#include <string.h>
#include "qemu/osdep.h"
#include "qapi/error.h"
#include "io/mpqemu-link.h"
#include "exec/memory.h"
#include "exec/cpu-common.h"
#include "exec/address-spaces.h"
#include "qemu/int128.h"
#include "qemu/range.h"
#include "hw/pci/pci.h"
#include "qemu/option.h"
#include "qemu/config-file.h"
#include "qapi/qmp/qjson.h"
#include "qapi/qmp/qstring.h"
#include "sysemu/sysemu.h"
#include "hw/proxy/qemu-proxy.h"
#include "hw/proxy/memory-sync.h"
#include "qom/object.h"

static void pci_proxy_dev_realize(PCIDevice *dev, Error **errp);

static int add_argv(char *command_str, char **argv, int argc)
{
    int max_args = 64;

    if (argc < max_args - 1) {
        argv[argc++] = command_str;
        argv[argc] = 0;
    } else {
        return 0;
    }

    return argc;
}

static int make_argv(char *command_str, char **argv, int argc)
{
    int max_args = 64;

    char *p2 = strtok(command_str, " ");
    while (p2 && argc < max_args - 1) {
        argv[argc++] = p2;
        p2 = strtok(0, " ");
    }
    argv[argc] = 0;

    return argc;
}

int remote_spawn(PCIProxyDev *pdev, const char *command, Error **errp)
{
    pid_t rpid;
    int fd[2] = {-1, -1};
    Error *local_error = NULL;
    char *argv[64];
    int argc = 0, _argc;
    char *sfd;
    char *exec_dir;
    int rc = -EINVAL;

    if (pdev->managed) {
        /* Child is forked by external program (such as libvirt). */
        return rc;
    }

    if (socketpair(AF_UNIX, SOCK_STREAM, 0, fd)) {
        error_setg(errp, "Unable to create unix socket.");
        return rc;
    }
    exec_dir = g_strdup_printf("%s/%s", qemu_get_exec_dir(), "qemu-scsi-dev");
    argc = add_argv(exec_dir, argv, argc);
    sfd = g_strdup_printf("%d", fd[1]);
    argc = add_argv(sfd, argv, argc);
    _argc = argc;
    argc = make_argv((char *)command, argv, argc);

    /* TODO: Restrict the forked process' permissions and capabilities. */
    rpid = qemu_fork(&local_error);

    if (rpid == -1) {
        error_setg(errp, "Unable to spawn emulation program.");
        close(fd[0]);
        goto fail;
    }

    if (rpid == 0) {
        close(fd[0]);
        execvp(argv[0], (char *const *)argv);
        exit(1);
    }
    pdev->remote_pid = rpid;
    pdev->rsocket = fd[1];
    pdev->socket = fd[0];

    rc = 0;

fail:
    close(fd[1]);

    for (int i = 0; i < _argc; i++) {
        g_free(argv[i]);
    }

    return rc;
}

static int get_proxy_sock(PCIDevice *dev)
{
    PCIProxyDev *pdev;

    pdev = PCI_PROXY_DEV(dev);

    return pdev->socket;
}

static void set_proxy_sock(PCIDevice *dev, int socket)
{
    PCIProxyDev *pdev;

    pdev = PCI_PROXY_DEV(dev);

    pdev->socket = socket;
    pdev->managed = true;

}

static int config_op_send(PCIProxyDev *dev, uint32_t addr, uint32_t *val, int l,
                          unsigned int op)
{
    MPQemuMsg msg;
    struct conf_data_msg conf_data;
    int wait;

    memset(&msg, 0, sizeof(MPQemuMsg));
    conf_data.addr = addr;
    conf_data.val = (op == CONF_WRITE) ? *val : 0;
    conf_data.l = l;

    msg.data2 = (uint8_t *)malloc(sizeof(conf_data));
    if (!msg.data2) {
        return -ENOMEM;
    }

    memcpy(msg.data2, (const uint8_t *)&conf_data, sizeof(conf_data));
    msg.size = sizeof(conf_data);
    msg.cmd = op;
    msg.bytestream = 1;

    if (op == CONF_WRITE) {
        msg.num_fds = 0;
    } else {
        wait = GET_REMOTE_WAIT;
        msg.num_fds = 1;
        msg.fds[0] = wait;
    }

    mpqemu_msg_send(dev->mpqemu_link, &msg, dev->mpqemu_link->com);

    if (op == CONF_READ) {
        *val = (uint32_t)wait_for_remote(wait);
        PUT_REMOTE_WAIT(wait);
    }

    free(msg.data2);

    return 0;
}

static uint32_t pci_proxy_read_config(PCIDevice *d, uint32_t addr, int len)
{
    uint32_t val;

    (void)pci_default_read_config(d, addr, len);

    config_op_send(PCI_PROXY_DEV(d), addr, &val, len, CONF_READ);

    return val;
}

static void pci_proxy_write_config(PCIDevice *d, uint32_t addr, uint32_t val,
                                   int l)
{
    pci_default_write_config(d, addr, val, l);

    config_op_send(PCI_PROXY_DEV(d), addr, &val, l, CONF_WRITE);
}

static void pci_proxy_dev_class_init(ObjectClass *klass, void *data)
{
    PCIDeviceClass *k = PCI_DEVICE_CLASS(klass);

    k->realize = pci_proxy_dev_realize;
    k->config_read = pci_proxy_read_config;
    k->config_write = pci_proxy_write_config;
}

static const TypeInfo pci_proxy_dev_type_info = {
    .name          = TYPE_PCI_PROXY_DEV,
    .parent        = TYPE_PCI_DEVICE,
    .instance_size = sizeof(PCIProxyDev),
    .abstract      = true,
    .class_size    = sizeof(PCIProxyDevClass),
    .class_init    = pci_proxy_dev_class_init,
    .interfaces = (InterfaceInfo[]) {
        { INTERFACE_CONVENTIONAL_PCI_DEVICE },
        { },
    },
};

static void pci_proxy_dev_register_types(void)
{
    type_register_static(&pci_proxy_dev_type_info);
}

type_init(pci_proxy_dev_register_types)

static void init_proxy(PCIDevice *dev, char *command, bool need_spawn, Error **errp)
{
    PCIProxyDev *pdev = PCI_PROXY_DEV(dev);
    Error *local_error = NULL;

    if (!pdev->managed) {
        if (need_spawn) {
            if (!remote_spawn(pdev, command, &local_error)) {
                return;
            }
        }
    } else {
        pdev->remote_pid = atoi(pdev->rid);
        if (pdev->remote_pid == -1) {
            error_setg(errp, "Remote PID is -1");
            return;
        }
    }

    pdev->mpqemu_link = mpqemu_link_create();

    if (!pdev->mpqemu_link) {
        error_setg(errp, "Failed to create proxy link");
        return;
    }

    mpqemu_init_channel(pdev->mpqemu_link, &pdev->mpqemu_link->com,
                        pdev->socket);

    configure_memory_sync(pdev->sync, pdev->mpqemu_link);
}

static void pci_proxy_dev_realize(PCIDevice *device, Error **errp)
{
    PCIProxyDev *dev = PCI_PROXY_DEV(device);
    PCIProxyDevClass *k = PCI_PROXY_DEV_GET_CLASS(dev);
    Error *local_err = NULL;
    int r;

    if (k->realize) {
        k->realize(dev, &local_err);
        if (local_err) {
            error_propagate(errp, local_err);
        }
    }

    for (r = 0; r < PCI_NUM_REGIONS; r++) {
        if (!dev->region[r].present) {
            continue;
        }

        dev->region[r].dev = dev;

        pci_register_bar(PCI_DEVICE(dev), r, dev->region[r].type,
                         &dev->region[r].mr);
    }

    dev->set_proxy_sock = set_proxy_sock;
    dev->get_proxy_sock = get_proxy_sock;
    dev->init_proxy = init_proxy;
    dev->sync = REMOTE_MEM_SYNC(object_new(TYPE_MEMORY_LISTENER));
}

static void send_bar_access_msg(PCIProxyDev *dev, MemoryRegion *mr,
                                bool write, hwaddr addr, uint64_t *val,
                                unsigned size, bool memory)
{
    MPQemuLinkState *mpqemu_link = dev->mpqemu_link;
    MPQemuMsg msg;
    int wait;

    memset(&msg, 0, sizeof(MPQemuMsg));

    msg.bytestream = 0;
    msg.size = sizeof(msg.data1);
    msg.data1.bar_access.addr = mr->addr + addr;
    msg.data1.bar_access.size = size;
    msg.data1.bar_access.memory = memory;

    if (write) {
        msg.cmd = BAR_WRITE;
        msg.data1.bar_access.val = *val;
    } else {
        wait = GET_REMOTE_WAIT;

        msg.cmd = BAR_READ;
        msg.num_fds = 1;
        msg.fds[0] = wait;
    }

    mpqemu_msg_send(mpqemu_link, &msg, mpqemu_link->com);

    if (!write) {
        *val = wait_for_remote(wait);
        PUT_REMOTE_WAIT(wait);
    }
}

void proxy_default_bar_write(void *opaque, hwaddr addr, uint64_t val,
                             unsigned size)
{
    ProxyMemoryRegion *pmr = opaque;

    send_bar_access_msg(pmr->dev, &pmr->mr, true, addr, &val, size,
                        pmr->memory);
}

uint64_t proxy_default_bar_read(void *opaque, hwaddr addr, unsigned size)
{
    ProxyMemoryRegion *pmr = opaque;
    uint64_t val;

    send_bar_access_msg(pmr->dev, &pmr->mr, false, addr, &val, size,
                        pmr->memory);

     return val;
}

const MemoryRegionOps proxy_default_ops = {
    .read = proxy_default_bar_read,
    .write = proxy_default_bar_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
    .impl = {
        .min_access_size = 1,
        .max_access_size = 1,
    },
};
