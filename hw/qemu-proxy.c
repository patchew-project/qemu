/*
 * Copyright 2018, Oracle and/or its affiliates. All rights reserved.
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
#include "io/proxy-link.h"
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
#include "hw/qemu-proxy.h"

char command[] = "qemu-scsi-dev";

static void pci_proxy_dev_realize(PCIDevice *dev, Error **errp);

int config_op_send(PCIProxyDev *dev, uint32_t addr, uint32_t val, int l,
                                                        unsigned int op)
{
    ProcMsg msg;
    struct conf_data_msg conf_data;

    conf_data.addr = addr;
    conf_data.val = val;
    conf_data.l = l;


    msg.data2 = (uint8_t *)malloc(sizeof(conf_data));
    if (!msg.data2) {
        printf("Failed to allocate memory for msg.data2\n");
        return -ENOMEM;
    }
    memcpy(msg.data2, (const uint8_t *)&conf_data, sizeof(conf_data));
    msg.size = sizeof(conf_data);
    msg.num_fds = 0;
    msg.cmd = op;
    msg.bytestream = 1;

    proxy_proc_send(dev->proxy_dev.proxy_link, &msg);
    free(msg.data2);

    return 0;
}

static uint32_t pci_proxy_read_config(PCIDevice *d,
                                       uint32_t addr, int len)
{
    config_op_send(PCI_PROXY_DEV(d), addr, 0, 0, CONF_READ);
    return pci_default_read_config(d, addr, len);
}

static void pci_proxy_write_config(PCIDevice *d, uint32_t addr, uint32_t val,
                                    int l)
{
    pci_default_write_config(d, addr, val, l);
    config_op_send(PCI_PROXY_DEV(d), addr, val, l, CONF_WRITE);
}


static void pci_proxy_dev_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    PCIDeviceClass *k = PCI_DEVICE_CLASS(klass);

    k->realize = pci_proxy_dev_realize;
    k->class_id = PCI_CLASS_SYSTEM_OTHER;
    k->vendor_id = PCI_VENDOR_ID_REDHAT;
    k->device_id = PCI_DEVICE_ID_REDHAT_TEST;
    k->config_read = pci_proxy_read_config;
    k->config_write = pci_proxy_write_config;

    set_bit(DEVICE_CATEGORY_MISC, dc->categories);
    dc->desc = "PCI Proxy Device";
}

static const TypeInfo pci_proxy_dev_type_info = {
    .name          = TYPE_PCI_PROXY_DEV,
    .parent        = TYPE_PCI_DEVICE,
    .instance_size = sizeof(PCIProxyDev),
    .class_init    = pci_proxy_dev_class_init,
    .interfaces = (InterfaceInfo[]) {
        { INTERFACE_CONVENTIONAL_PCI_DEVICE },
        { },
    }
};

static void pci_proxy_mmio_write(void *opaque, hwaddr addr, uint64_t val,
                                    unsigned size)
{
    return;
}

static uint64_t pci_proxy_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    return 0;
}

static const MemoryRegionOps proxy_device_mmio_ops = {
    .read = pci_proxy_mmio_read,
    .write = pci_proxy_mmio_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
    .impl = {
        .min_access_size = 1,
    }
};

static void pci_proxy_dev_register_types(void)
{
    type_register_static(&pci_proxy_dev_type_info);
}

static void pci_proxy_dev_realize(PCIDevice *device, Error **errp)
{
    PCIProxyDev *dev = PCI_PROXY_DEV(device);
    uint8_t *pci_conf;

    pci_conf = device->config;

    init_emulation_process(dev, command, errp);
    if (*errp) {
        printf("Process did not start\n");
        error_report_err(*errp);
    }

    if (!dev->proxy_dev.proxy_link) {
        printf("Proxy link is not set\n");
    }

    configure_memory_listener(dev);

    pci_conf[PCI_LATENCY_TIMER] = 0xff;

    memory_region_init_io(&dev->mmio_io, OBJECT(dev), &proxy_device_mmio_ops,
                          dev, "proxy-device-mmio", 0x400);

    pci_register_bar(device, 0, PCI_BASE_ADDRESS_SPACE_MEMORY, &dev->mmio_io);

}

void init_emulation_process(PCIProxyDev *pdev, char *command, Error **errp)
{
    char *args[2];
    pid_t rpid;
    int fd[2];

    if (socketpair(AF_UNIX, SOCK_STREAM, 0, fd)) {
        error_setg(errp, "Unable to create unix socket.");
        return;
    }

    rpid = qemu_fork(errp);

    if (rpid == -1) {
        error_setg(errp, "Unable to spawn emulation program.");
        return;
    }

    if (rpid == 0) {
        if (dup2(fd[1], STDIN_FILENO) != STDIN_FILENO) {
            perror("Failed to acquire socket.");
            exit(1);
        }

        close(fd[0]);
        close(fd[1]);

        args[0] = command;
        args[1] = NULL;
        execvp(args[0], (char *const *)args);
        exit(1);
    }

    pdev->proxy_dev.proxy_link = proxy_link_create();

    if (!pdev->proxy_dev.proxy_link) {
        return;
    }

    proxy_link_set_sock(pdev->proxy_dev.proxy_link, fd[0]);
    close(fd[1]);

}

type_init(pci_proxy_dev_register_types)

static void proxy_ml_begin(MemoryListener *listener)
{
    int mrs;
    struct proxy_device *pdev = container_of(listener, struct proxy_device,
                                             memory_listener);

    for (mrs = 0; mrs < pdev->n_mr_sections; mrs++) {
        memory_region_unref(pdev->mr_sections[mrs].mr);
    }

    g_free(pdev->mr_sections);
    pdev->mr_sections = NULL;
    pdev->n_mr_sections = 0;
}

static bool proxy_mrs_can_merge(uint64_t host, uint64_t prev_host, size_t size)
{
    bool merge;
    ram_addr_t offset;
    int fd1, fd2;
    MemoryRegion *mr;

    mr = memory_region_from_host((void *)(uintptr_t)host, &offset);
    fd1 = memory_region_get_fd(mr);

    mr = memory_region_from_host((void *)(uintptr_t)prev_host, &offset);
    fd2 = memory_region_get_fd(mr);

    merge = (fd1 == fd2);

    merge &= ((prev_host + size) == host);

    return merge;
}

static void proxy_ml_region_addnop(MemoryListener *listener,
                                   MemoryRegionSection *section)
{
    bool need_add = true;
    uint64_t mrs_size, mrs_gpa, mrs_page;
    uintptr_t mrs_host;
    RAMBlock *mrs_rb;
    MemoryRegionSection *prev_sec;
    struct proxy_device *pdev = container_of(listener, struct proxy_device,
                                             memory_listener);

    if (!(memory_region_is_ram(section->mr) &&
          !memory_region_is_rom(section->mr))) {
        return;
    }

    mrs_rb = section->mr->ram_block;
    mrs_page = (uint64_t)qemu_ram_pagesize(mrs_rb);
    mrs_size = int128_get64(section->size);
    mrs_gpa = section->offset_within_address_space;
    mrs_host = (uintptr_t)memory_region_get_ram_ptr(section->mr) +
               section->offset_within_region;

    mrs_host = mrs_host & ~(mrs_page - 1);
    mrs_gpa = mrs_gpa & ~(mrs_page - 1);
    mrs_size = ROUND_UP(mrs_size, mrs_page);

    if (pdev->n_mr_sections) {
        prev_sec = pdev->mr_sections + (pdev->n_mr_sections - 1);
        uint64_t prev_gpa_start = prev_sec->offset_within_address_space;
        uint64_t prev_size = int128_get64(prev_sec->size);
        uint64_t prev_gpa_end   = range_get_last(prev_gpa_start, prev_size);
        uint64_t prev_host_start =
            (uintptr_t)memory_region_get_ram_ptr(prev_sec->mr) +
            prev_sec->offset_within_region;
        uint64_t prev_host_end = range_get_last(prev_host_start, prev_size);

        if (mrs_gpa <= (prev_gpa_end + 1)) {
            if (mrs_gpa < prev_gpa_start) {
                assert(0);
            }

            if ((section->mr == prev_sec->mr) &&
                proxy_mrs_can_merge(mrs_host, prev_host_start,
                                    (mrs_gpa - prev_gpa_start))) {
                uint64_t max_end = MAX(prev_host_end, mrs_host + mrs_size);
               need_add = false;
                prev_sec->offset_within_address_space =
                    MIN(prev_gpa_start, mrs_gpa);
                prev_sec->offset_within_region =
                    MIN(prev_host_start, mrs_host) -
                    (uintptr_t)memory_region_get_ram_ptr(prev_sec->mr);
                prev_sec->size = int128_make64(max_end - MIN(prev_host_start,
                                                             mrs_host));
            }
        }
    }

    if (need_add) {
        ++pdev->n_mr_sections;
        pdev->mr_sections = g_renew(MemoryRegionSection, pdev->mr_sections,
                                    pdev->n_mr_sections);
        pdev->mr_sections[pdev->n_mr_sections - 1] = *section;
        pdev->mr_sections[pdev->n_mr_sections - 1].fv = NULL;
        memory_region_ref(section->mr);
    }
}

static void proxy_ml_commit(MemoryListener *listener)
{
    ProcMsg msg;
    ram_addr_t offset;
    MemoryRegion *mr;
    MemoryRegionSection section;
    uintptr_t host_addr;
    int region;
    struct proxy_device *pdev = container_of(listener, struct proxy_device,
                                             memory_listener);

    msg.cmd = SYNC_SYSMEM;
    msg.bytestream = 0;
    msg.num_fds = pdev->n_mr_sections;
    assert(msg.num_fds <= MAX_FDS);

    for (region = 0; region < pdev->n_mr_sections; region++) {
        section = pdev->mr_sections[region];
        msg.data1.sync_sysmem.gpas[region] =
            section.offset_within_address_space;
        msg.data1.sync_sysmem.sizes[region] = int128_get64(section.size);
        host_addr = (uintptr_t)memory_region_get_ram_ptr(section.mr) +
                    section.offset_within_region;
        mr = memory_region_from_host((void *)host_addr, &offset);
        msg.fds[region] = memory_region_get_fd(mr);
    }
    proxy_proc_send(pdev->proxy_link, &msg);
}

void deconfigure_memory_listener(PCIProxyDev *pdev)
{
    memory_listener_unregister(&pdev->proxy_dev.memory_listener);
}

static MemoryListener proxy_listener = {
        .begin = proxy_ml_begin,
        .commit = proxy_ml_commit,
        .region_add = proxy_ml_region_addnop,
        .region_nop = proxy_ml_region_addnop,
        .priority = 10,
};

void configure_memory_listener(PCIProxyDev *dev)
{
    dev->proxy_dev.memory_listener = proxy_listener;
    dev->proxy_dev.n_mr_sections = 0;
    dev->proxy_dev.mr_sections = NULL;
    memory_listener_register(&dev->proxy_dev.memory_listener,
                             &address_space_memory);
}
