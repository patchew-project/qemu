/*
 * Remote IO Hub
 *
 * Copyright © 2018, 2020 Oracle and/or its affiliates.
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 *
 */

#include "qemu/osdep.h"
#include "qemu-common.h"

#include "hw/pci/pci.h"
#include "hw/pci/pci_ids.h"
#include "hw/pci/pci_bus.h"
#include "hw/remote/iohub.h"
#include "qemu/thread.h"
#include "hw/boards.h"
#include "hw/i386/remote.h"
#include "qemu/main-loop.h"

static void remote_iohub_initfn(Object *obj)
{
    RemoteIOHubState *iohub = REMOTE_IOHUB_DEVICE(obj);
    int slot, intx, pirq;

    memset(&iohub->irqfds, 0, sizeof(iohub->irqfds));
    memset(&iohub->resamplefds, 0, sizeof(iohub->resamplefds));

    for (slot = 0; slot < PCI_SLOT_MAX; slot++) {
        for (intx = 0; intx < PCI_NUM_PINS; intx++) {
            iohub->irq_num[slot][intx] = (slot + intx) % 4 + 4;
        }
    }

    for (pirq = 0; pirq < REMOTE_IOHUB_NB_PIRQS; pirq++) {
        qemu_mutex_init(&iohub->irq_level_lock[pirq]);
        iohub->irq_level[pirq] = 0;
        event_notifier_init_fd(&iohub->irqfds[pirq], -1);
        event_notifier_init_fd(&iohub->resamplefds[pirq], -1);
    }
}

static void remote_iohub_class_init(ObjectClass *klass, void *data)
{
    PCIDeviceClass *k = PCI_DEVICE_CLASS(klass);
    k->vendor_id = PCI_VENDOR_ID_ORACLE;
    k->device_id = PCI_DEVICE_ID_REMOTE_IOHUB;
}

static const TypeInfo remote_iohub_info = {
    .name       = TYPE_REMOTE_IOHUB_DEVICE,
    .parent     = TYPE_PCI_DEVICE,
    .instance_size = sizeof(RemoteIOHubState),
    .instance_init = remote_iohub_initfn,
    .class_init  = remote_iohub_class_init,
    .interfaces = (InterfaceInfo[]) {
        { INTERFACE_CONVENTIONAL_PCI_DEVICE },
        { }
    }
};

static void remote_iohub_register(void)
{
    type_register_static(&remote_iohub_info);
}

type_init(remote_iohub_register);

int remote_iohub_map_irq(PCIDevice *pci_dev, int intx)
{
    BusState *bus = qdev_get_parent_bus(&pci_dev->qdev);
    PCIBus *pci_bus = PCI_BUS(bus);
    PCIDevice *pci_iohub =
        pci_bus->devices[PCI_DEVFN(REMOTE_IOHUB_DEV, REMOTE_IOHUB_FUNC)];
    RemoteIOHubState *iohub = REMOTE_IOHUB_DEVICE(pci_iohub);

    return iohub->irq_num[PCI_SLOT(pci_dev->devfn)][intx];
}

/*
 * TODO: Using lock to set the interrupt level could become a
 *       performance bottleneck. Check if atomic arithmetic
 *       is possible.
 */
void remote_iohub_set_irq(void *opaque, int pirq, int level)
{
    RemoteIOHubState *iohub = opaque;

    assert(pirq >= 0);
    assert(pirq < REMOTE_IOHUB_NB_PIRQS);

    qemu_mutex_lock(&iohub->irq_level_lock[pirq]);

    if (level) {
        if (++iohub->irq_level[pirq] == 1) {
            event_notifier_set(&iohub->irqfds[pirq]);
        }
    } else if (iohub->irq_level[pirq] > 0) {
        iohub->irq_level[pirq]--;
    }

    qemu_mutex_unlock(&iohub->irq_level_lock[pirq]);
}

static void intr_resample_handler(void *opaque)
{
    ResampleToken *token = opaque;
    RemoteIOHubState *iohub = token->iohub;
    int pirq, s;

    pirq = token->pirq;

    s = event_notifier_test_and_clear(&iohub->resamplefds[pirq]);

    assert(s >= 0);

    qemu_mutex_lock(&iohub->irq_level_lock[pirq]);

    if (iohub->irq_level[pirq]) {
        event_notifier_set(&iohub->irqfds[pirq]);
    }

    qemu_mutex_unlock(&iohub->irq_level_lock[pirq]);
}

void process_set_irqfd_msg(PCIDevice *pci_dev, MPQemuMsg *msg)
{
    RemMachineState *machine = REMOTE_MACHINE(current_machine);
    RemoteIOHubState *iohub = machine->iohub;
    int pirq;

    g_assert(msg->data1.set_irqfd.intx < 4);
    g_assert(msg->num_fds == 2);

    pirq = remote_iohub_map_irq(pci_dev, msg->data1.set_irqfd.intx);

    if (event_notifier_get_fd(&iohub->irqfds[pirq]) != -1) {
        event_notifier_cleanup(&iohub->irqfds[pirq]);
        event_notifier_cleanup(&iohub->resamplefds[pirq]);
        memset(&iohub->token[pirq], 0, sizeof(ResampleToken));
    }

    event_notifier_init_fd(&iohub->irqfds[pirq], msg->fds[0]);
    event_notifier_init_fd(&iohub->resamplefds[pirq], msg->fds[1]);

    iohub->token[pirq].iohub = iohub;
    iohub->token[pirq].pirq = pirq;

    qemu_set_fd_handler(msg->fds[1], intr_resample_handler, NULL,
                        &iohub->token[pirq]);
}
