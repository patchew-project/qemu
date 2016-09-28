/*
 * Guest-side management of hypertrace.
 *
 * Copyright (C) 2016 Lluís Vilanova <vilanova@ac.upc.edu>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation, either version 3 of the
 * License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see http://www.gnu.org/licenses/.
 */

#include <linux/qemu-hypertrace.h>

#include <linux/module.h>
#include <linux/pci.h>
#include <linux/uaccess.h>

#define xstr(s) #s
#define str(s) xstr(s)
#include str(COMMON_H)


#define VERSION_STR "0.1"
#define PCI_VENDOR_ID_REDHAT_QUMRANET    0x1af4
#define PCI_DEVICE_ID_HYPERTRACE         0x10f0


MODULE_DESCRIPTION("Kernel interface to QEMU's hypertrace device");
MODULE_LICENSE("GPL");
MODULE_AUTHOR("Lluís Vilanova");
MODULE_VERSION(VERSION_STR);


/*********************************************************************
 * Kernel interface
 */

uint64_t _qemu_hypertrace_channel_max_clients;
uint64_t _qemu_hypertrace_channel_num_args;
static uint64_t *_qemu_hypertrace_channel_config;
uint64_t *_qemu_hypertrace_channel_data;
uint64_t *_qemu_hypertrace_channel_control;

EXPORT_SYMBOL(_qemu_hypertrace_channel_max_clients);
EXPORT_SYMBOL(_qemu_hypertrace_channel_num_args);
EXPORT_SYMBOL(_qemu_hypertrace_channel_data);
EXPORT_SYMBOL(_qemu_hypertrace_channel_control);


/*********************************************************************
 * Channel initialization
 */

static int init_channel(uint64_t **vaddr, struct pci_dev *dev, int bar)
{
    void *res;
    resource_size_t start, size;

    start = pci_resource_start(dev, bar);
    size = pci_resource_len(dev, bar);

    if (start == 0 || size == 0) {
        return -ENOENT;
    }

    res = ioremap(start, size);
    if (res == 0) {
        return -EINVAL;
    }

    *vaddr = res;
    return 0;
}

/*********************************************************************
 * Module (de)initialization
 */

int init_module(void)
{
    int res = 0;
    struct pci_dev *dev = NULL;
    struct hypertrace_config *config;

    printk(KERN_NOTICE "Loading QEMU hypertrace module (version %s)\n",
           VERSION_STR);

    dev = pci_get_device(PCI_VENDOR_ID_REDHAT_QUMRANET,
                         PCI_DEVICE_ID_HYPERTRACE,
                         NULL);
    if (dev == NULL) {
        res = -ENOENT;
        printk(KERN_ERR "Unable to find hypertrace device\n");
        goto error;
    }

    res = init_channel(&_qemu_hypertrace_channel_config, dev, 0);
    if (res != 0) {
        printk(KERN_ERR "Unable to find hypertrace config channel\n");
        goto error;
    }

    config = (struct hypertrace_config *)_qemu_hypertrace_channel_config;
    _qemu_hypertrace_channel_max_clients = config->max_clients;
    _qemu_hypertrace_channel_num_args = config->client_args;

    res = init_channel(&_qemu_hypertrace_channel_data, dev, 1);
    if (res != 0) {
        printk(KERN_ERR "Unable to find hypertrace data channel\n");
        goto error_config;
    }

    res = init_channel(&_qemu_hypertrace_channel_control, dev, 2);
    if (res != 0) {
        printk(KERN_ERR "Unable to find hypertrace control channel\n");
        goto error_data;
    }

    goto ok;

error_data:
    iounmap(_qemu_hypertrace_channel_data);

error_config:
    iounmap(_qemu_hypertrace_channel_config);
    _qemu_hypertrace_channel_config = NULL;
    _qemu_hypertrace_channel_data = NULL;

error:
ok:
    return res;
}

void cleanup_module(void)
{
    printk(KERN_NOTICE "Unloading QEMU hypertrace module\n");

    iounmap(_qemu_hypertrace_channel_config);
    iounmap(_qemu_hypertrace_channel_data);
    iounmap(_qemu_hypertrace_channel_control);
}
