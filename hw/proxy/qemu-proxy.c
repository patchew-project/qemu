/*
 * Copyright © 2018, 2020 Oracle and/or its affiliates.
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 *
 */

#include "qemu/osdep.h"
#include "qapi/error.h"
#include "io/mpqemu-link.h"
#include "exec/memory.h"
#include "exec/cpu-common.h"
#include "exec/address-spaces.h"
#include "hw/pci/pci.h"
#include "qapi/qmp/qjson.h"
#include "qapi/qmp/qstring.h"
#include "hw/proxy/qemu-proxy.h"
#include "hw/proxy/memory-sync.h"
#include "qom/object.h"

static void pci_proxy_dev_realize(PCIDevice *dev, Error **errp);

static int add_argv(char *opts_str, char **argv, int argc)
{
    int max_args = 64;

    if (argc < max_args - 1) {
        argv[argc++] = opts_str;
        argv[argc] = 0;
    } else {
        return 0;
    }

    return argc;
}

static int make_argv(char *opts_str, char **argv, int argc)
{
    int max_args = 64;

    char *p2 = strtok(opts_str, " ");
    while (p2 && argc < max_args - 1) {
        argv[argc++] = p2;
        p2 = strtok(0, " ");
    }
    argv[argc] = 0;

    return argc;
}

static int remote_spawn(PCIProxyDev *pdev, const char *opts,
                        const char *exec_name, Error **errp)
{
    pid_t rpid;
    int fd[2] = {-1, -1};
    Error *local_error = NULL;
    char *argv[64];
    int argc = 0;
    char *sfd;
    char *exec_dir;
    int rc = -EINVAL;

    if (pdev->managed) {
        /* Child is forked by external program (such as libvirt). */
        error_setg(errp, "Remote processed is managed and launched by external program");
        return rc;
    }

    if (!exec_name) {
        error_setg(errp, "The remote exec name is NULL.");
        return rc;
    }

    if (socketpair(AF_UNIX, SOCK_STREAM, 0, fd)) {
        error_setg(errp, "Unable to create unix socket.");
        return rc;
    }
    exec_dir = g_strdup_printf("%s/%s", qemu_get_exec_dir(), exec_name);
    argc = add_argv(exec_dir, argv, argc);
    sfd = g_strdup_printf("%d", fd[1]);
    argc = add_argv(sfd, argv, argc);
    argc = make_argv((char *)opts, argv, argc);

    /* TODO: Restrict the forked process' permissions and capabilities. */
    rpid = qemu_fork(&local_error);

    if (rpid == -1) {
        error_setg(errp, "Unable to spawn emulation program.");
        close(fd[0]);
        goto fail;
    }

    if (rpid == 0) {
        close(fd[0]);

        rc = execv(argv[0], (char *const *)argv);
        exit(1);
    }
    pdev->remote_pid = rpid;
    pdev->socket = fd[0];

    rc = 0;

fail:
    close(fd[1]);

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
    conf_data.val = (op == PCI_CONFIG_WRITE) ? *val : 0;
    conf_data.l = l;

    msg.data2 = (uint8_t *)&conf_data;
    if (!msg.data2) {
        return -ENOMEM;
    }

    msg.size = sizeof(conf_data);
    msg.cmd = op;
    msg.bytestream = 1;

    if (op == PCI_CONFIG_WRITE) {
        msg.num_fds = 0;
    } else {
        /* TODO: Dont create fd each time for send. */
        wait = GET_REMOTE_WAIT;
        msg.num_fds = 1;
        msg.fds[0] = wait;
    }

    mpqemu_msg_send(&msg, dev->mpqemu_link->com);

    if (op == PCI_CONFIG_READ) {
        *val = (uint32_t)wait_for_remote(wait);
        PUT_REMOTE_WAIT(wait);
    }

    return 0;
}

static uint32_t pci_proxy_read_config(PCIDevice *d, uint32_t addr, int len)
{
    uint32_t val;

    (void)pci_default_read_config(d, addr, len);

    config_op_send(PCI_PROXY_DEV(d), addr, &val, len, PCI_CONFIG_READ);

    return val;
}

static void pci_proxy_write_config(PCIDevice *d, uint32_t addr, uint32_t val,
                                   int l)
{
    pci_default_write_config(d, addr, val, l);

    config_op_send(PCI_PROXY_DEV(d), addr, &val, l, PCI_CONFIG_WRITE);
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

static void init_proxy(PCIDevice *dev, char *command, char *exec_name,
                       bool need_spawn, Error **errp)
{
    PCIProxyDev *pdev = PCI_PROXY_DEV(dev);
    Error *local_error = NULL;

    if (!pdev->managed) {
        if (need_spawn) {
            if (remote_spawn(pdev, command, exec_name, &local_error)) {
                error_propagate(errp, local_error);
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

    if (k->realize) {
        k->realize(dev, &local_err);
        if (local_err) {
            error_propagate(errp, local_err);
        }
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

    mpqemu_msg_send(&msg, mpqemu_link->com);

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
