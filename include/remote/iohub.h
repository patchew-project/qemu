/*
 * IO Hub for remote device
 *
 * Copyright 2019, Oracle and/or its affiliates. All rights reserved.
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

#ifndef REMOTE_IOHUB_H
#define REMOTE_IOHUB_H

#include <sys/types.h>

#include "qemu/osdep.h"
#include "hw/pci/pci.h"
#include "qemu/event_notifier.h"
#include "qemu/thread-posix.h"
#include "io/mpqemu-link.h"

#define REMOTE_IOHUB_NB_PIRQS    8

#define REMOTE_IOHUB_DEV         31
#define REMOTE_IOHUB_FUNC        0

#define TYPE_REMOTE_IOHUB_DEVICE "remote-iohub"
#define REMOTE_IOHUB_DEVICE(obj) \
    OBJECT_CHECK(RemoteIOHubState, (obj), TYPE_REMOTE_IOHUB_DEVICE)

typedef struct RemoteIOHubState {
    PCIDevice d;
    uint8_t irq_num[PCI_SLOT_MAX][PCI_NUM_PINS];
    EventNotifier irqfds[REMOTE_IOHUB_NB_PIRQS];
    EventNotifier resamplefds[REMOTE_IOHUB_NB_PIRQS];
    unsigned int irq_level[REMOTE_IOHUB_NB_PIRQS];
    QemuMutex irq_level_lock[REMOTE_IOHUB_NB_PIRQS];
} RemoteIOHubState;

typedef struct ResampleToken {
    RemoteIOHubState *iohub;
    int pirq;
} ResampleToken;

int remote_iohub_map_irq(PCIDevice *pci_dev, int intx);
void remote_iohub_set_irq(void *opaque, int pirq, int level);
void process_set_irqfd_msg(PCIDevice *pci_dev, MPQemuMsg *msg);

#endif
