/*
 * vfio PCI device - PCI interrupt handling
 *
 * Copyright Red Hat, Inc. 2012-2026
 *
 * Authors:
 *  Alex Williamson <alex.williamson@redhat.com>
 *
 * SPDX-License-Identifier: GPL-2.0-only
 */

#include "qemu/osdep.h"
#include <linux/vfio.h>

#include "hw/pci/msi.h"
#include "hw/pci/msix.h"
#include "hw/pci/pci_device.h"
#include "hw/vfio/vfio-cpr.h"
#include "migration/cpr.h"
#include "exec/cpu-common.h"
#include "qemu/error-report.h"
#include "qemu/main-loop.h"
#include "qemu/range.h"
#include "qemu/units.h"
#include "system/accel-irq.h"
#include "system/kvm.h"
#include "system/runstate.h"
#include "pci.h"
#include "trace.h"
#include "qapi/error.h"
#include "vfio-helpers.h"

/* Protected by BQL */
static AccelRouteChange vfio_route_change;

static void vfio_msi_disable_common(VFIOPCIDevice *vdev);

/* Create new or reuse existing eventfd */
static bool vfio_notifier_init(VFIOPCIDevice *vdev, EventNotifier *e,
                               const char *name, int nr, Error **errp)
{
    int fd, ret;

    fd = vfio_cpr_load_vector_fd(vdev, name, nr);
    if (fd >= 0) {
        event_notifier_init_fd(e, fd);
        return true;
    }

    ret = event_notifier_init(e, 0);
    if (ret < 0) {
        error_setg_errno(errp, -ret, "vfio_notifier_init %s failed", name);
        return false;
    }

    fd = event_notifier_get_fd(e);
    if (!vfio_cpr_save_vector_fd(vdev, name, nr, fd, errp)) {
        event_notifier_cleanup(e);
        return false;
    }
    return true;
}

static void vfio_notifier_cleanup(VFIOPCIDevice *vdev, EventNotifier *e,
                                  const char *name, int nr)
{
    vfio_cpr_delete_vector_fd(vdev, name, nr);
    event_notifier_cleanup(e);
}

/*
 * Disabling BAR mmaping can be slow, but toggling it around INTx can
 * also be a huge overhead.  We try to get the best of both worlds by
 * waiting until an interrupt to disable mmaps (subsequent transitions
 * to the same state are effectively no overhead).  If the interrupt has
 * been serviced and the time gap is long enough, we re-enable mmaps for
 * performance.  This works well for things like graphics cards, which
 * may not use their interrupt at all and are penalized to an unusable
 * level by read/write BAR traps.  Other devices, like NICs, have more
 * regular interrupts and see much better latency by staying in non-mmap
 * mode.  We therefore set the default mmap_timeout such that a ping
 * is just enough to keep the mmap disabled.  Users can experiment with
 * other options with the x-intx-mmap-timeout-ms parameter (a value of
 * zero disables the timer).
 */
static void vfio_intx_mmap_enable(void *opaque)
{
    VFIOPCIDevice *vdev = opaque;

    if (vdev->intx.pending) {
        timer_mod(vdev->intx.mmap_timer,
                       qemu_clock_get_ms(QEMU_CLOCK_VIRTUAL) + vdev->intx.mmap_timeout);
        return;
    }

    vfio_region_set_enabled(vdev, true);
}

static void vfio_intx_interrupt(void *opaque)
{
    VFIOPCIDevice *vdev = opaque;
    PCIDevice *pdev = PCI_DEVICE(vdev);

    if (!event_notifier_test_and_clear(&vdev->intx.interrupt)) {
        return;
    }

    trace_vfio_intx_interrupt(vdev->vbasedev.name, 'A' + vdev->intx.pin);

    vdev->intx.pending = true;
    pci_irq_assert(pdev);
    vfio_region_set_enabled(vdev, false);
    if (vdev->intx.mmap_timeout) {
        timer_mod(vdev->intx.mmap_timer,
                       qemu_clock_get_ms(QEMU_CLOCK_VIRTUAL) + vdev->intx.mmap_timeout);
    }
}

void vfio_pci_intx_eoi(VFIODevice *vbasedev)
{
    VFIOPCIDevice *vdev = container_of(vbasedev, VFIOPCIDevice, vbasedev);
    PCIDevice *pdev = PCI_DEVICE(vdev);

    if (!vdev->intx.pending) {
        return;
    }

    trace_vfio_pci_intx_eoi(vbasedev->name);

    vdev->intx.pending = false;
    pci_irq_deassert(pdev);
    vfio_device_irq_unmask(vbasedev, VFIO_PCI_INTX_IRQ_INDEX);
}

static bool vfio_intx_enable_kvm(VFIOPCIDevice *vdev, Error **errp)
{
    PCIDevice *pdev = PCI_DEVICE(vdev);
    int irq_fd = event_notifier_get_fd(&vdev->intx.interrupt);

    if (vdev->no_kvm_intx || !kvm_irqfds_enabled() ||
        vdev->intx.route.mode != PCI_INTX_ENABLED ||
        !kvm_resamplefds_enabled()) {
        return true;
    }

    /* Get to a known interrupt state */
    qemu_set_fd_handler(irq_fd, NULL, NULL, vdev);
    vfio_device_irq_mask(&vdev->vbasedev, VFIO_PCI_INTX_IRQ_INDEX);
    vdev->intx.pending = false;
    pci_irq_deassert(pdev);

    /* Get an eventfd for resample/unmask */
    if (!vfio_notifier_init(vdev, &vdev->intx.unmask, "intx-unmask", 0, errp)) {
        goto fail;
    }

    if (kvm_irqchip_add_irqfd_notifier_gsi(kvm_state,
                                           &vdev->intx.interrupt,
                                           &vdev->intx.unmask,
                                           vdev->intx.route.irq)) {
        error_setg_errno(errp, errno, "failed to setup resample irqfd");
        goto fail_irqfd;
    }

    if (!vfio_device_irq_set_signaling(&vdev->vbasedev, VFIO_PCI_INTX_IRQ_INDEX, 0,
                                       VFIO_IRQ_SET_ACTION_UNMASK,
                                       event_notifier_get_fd(&vdev->intx.unmask),
                                       errp)) {
        goto fail_vfio;
    }

    /* Let'em rip */
    vfio_device_irq_unmask(&vdev->vbasedev, VFIO_PCI_INTX_IRQ_INDEX);

    vdev->intx.kvm_accel = true;

    trace_vfio_intx_enable_kvm(vdev->vbasedev.name);

    return true;

fail_vfio:
    kvm_irqchip_remove_irqfd_notifier_gsi(kvm_state, &vdev->intx.interrupt,
                                          vdev->intx.route.irq);
fail_irqfd:
    vfio_notifier_cleanup(vdev, &vdev->intx.unmask, "intx-unmask", 0);
fail:
    qemu_set_fd_handler(irq_fd, vfio_intx_interrupt, NULL, vdev);
    vfio_device_irq_unmask(&vdev->vbasedev, VFIO_PCI_INTX_IRQ_INDEX);
    return false;
}

static bool vfio_cpr_intx_enable_kvm(VFIOPCIDevice *vdev, Error **errp)
{
    if (vdev->no_kvm_intx || !kvm_irqfds_enabled() ||
        vdev->intx.route.mode != PCI_INTX_ENABLED ||
        !kvm_resamplefds_enabled()) {
        return true;
    }

    if (!vfio_notifier_init(vdev, &vdev->intx.unmask, "intx-unmask", 0, errp)) {
        return false;
    }

    if (kvm_irqchip_add_irqfd_notifier_gsi(kvm_state,
                                           &vdev->intx.interrupt,
                                           &vdev->intx.unmask,
                                           vdev->intx.route.irq)) {
        error_setg_errno(errp, errno, "failed to setup resample irqfd");
        vfio_notifier_cleanup(vdev, &vdev->intx.unmask, "intx-unmask", 0);
        return false;
    }

    vdev->intx.kvm_accel = true;
    trace_vfio_intx_enable_kvm(vdev->vbasedev.name);
    return true;
}

static void vfio_intx_disable_kvm(VFIOPCIDevice *vdev)
{
    PCIDevice *pdev = PCI_DEVICE(vdev);

    if (!vdev->intx.kvm_accel) {
        return;
    }

    /*
     * Get to a known state, hardware masked, QEMU ready to accept new
     * interrupts, QEMU IRQ de-asserted.
     */
    vfio_device_irq_mask(&vdev->vbasedev, VFIO_PCI_INTX_IRQ_INDEX);
    vdev->intx.pending = false;
    pci_irq_deassert(pdev);

    /* Tell KVM to stop listening for an INTx irqfd */
    if (kvm_irqchip_remove_irqfd_notifier_gsi(kvm_state, &vdev->intx.interrupt,
                                              vdev->intx.route.irq)) {
        error_report("vfio: Error: Failed to disable INTx irqfd: %m");
    }

    /* We only need to close the eventfd for VFIO to cleanup the kernel side */
    vfio_notifier_cleanup(vdev, &vdev->intx.unmask, "intx-unmask", 0);

    /* QEMU starts listening for interrupt events. */
    qemu_set_fd_handler(event_notifier_get_fd(&vdev->intx.interrupt),
                        vfio_intx_interrupt, NULL, vdev);

    vdev->intx.kvm_accel = false;

    /* If we've missed an event, let it re-fire through QEMU */
    vfio_device_irq_unmask(&vdev->vbasedev, VFIO_PCI_INTX_IRQ_INDEX);

    trace_vfio_intx_disable_kvm(vdev->vbasedev.name);
}

static void vfio_intx_update(VFIOPCIDevice *vdev, PCIINTxRoute *route)
{
    Error *err = NULL;

    trace_vfio_intx_update(vdev->vbasedev.name,
                           vdev->intx.route.irq, route->irq);

    if (kvm_enabled()) {
        vfio_intx_disable_kvm(vdev);
    }

    vdev->intx.route = *route;

    if (route->mode != PCI_INTX_ENABLED) {
        return;
    }

    if (kvm_enabled() && !vfio_intx_enable_kvm(vdev, &err)) {
        warn_reportf_err(err, VFIO_MSG_PREFIX, vdev->vbasedev.name);
    }

    /* Re-enable the interrupt in cased we missed an EOI */
    vfio_pci_intx_eoi(&vdev->vbasedev);
}

static void vfio_intx_routing_notifier(PCIDevice *pdev)
{
    VFIOPCIDevice *vdev = VFIO_PCI_DEVICE(pdev);
    PCIINTxRoute route;

    if (vdev->interrupt != VFIO_INT_INTx) {
        return;
    }

    route = pci_device_route_intx_to_irq(pdev, vdev->intx.pin);

    if (pci_intx_route_changed(&vdev->intx.route, &route)) {
        vfio_intx_update(vdev, &route);
    }
}

static void vfio_irqchip_change(Notifier *notify, void *data)
{
    VFIOPCIDevice *vdev = container_of(notify, VFIOPCIDevice,
                                       irqchip_change_notifier);

    vfio_intx_update(vdev, &vdev->intx.route);
}

bool vfio_intx_enable(VFIOPCIDevice *vdev, Error **errp)
{
    PCIDevice *pdev = PCI_DEVICE(vdev);
    uint32_t val = vfio_pci_read_config(pdev, PCI_INTERRUPT_PIN, 1);
    uint8_t pin;
    Error *err = NULL;
    int32_t fd;

    if (val == (uint32_t)-1) {
        error_setg(errp, "failed to read PCI_INTERRUPT_PIN");
        return false;
    }
    pin = val;

    if (!pin) {
        return true;
    }

    if (pin > PCI_NUM_PINS) {
        error_setg(errp, "invalid PCI interrupt pin %d", pin);
        return false;
    }

    /*
     * Do not alter interrupt state during vfio_realize and cpr load.
     * The incoming state is cleared thereafter.
     */
    if (!cpr_is_incoming()) {
        vfio_disable_interrupts(vdev);
    }

    vdev->intx.pin = pin - 1; /* Pin A (1) -> irq[0] */
    pci_config_set_interrupt_pin(pdev->config, pin);

    /*
     * Only conditional to avoid generating error messages on platforms
     * where we won't actually use the result anyway.
     */
    if (kvm_enabled() && kvm_irqfds_enabled() && kvm_resamplefds_enabled()) {
        vdev->intx.route = pci_device_route_intx_to_irq(pdev,
                                                        vdev->intx.pin);
    }

    if (!vfio_notifier_init(vdev, &vdev->intx.interrupt, "intx-interrupt", 0,
                            errp)) {
        return false;
    }
    fd = event_notifier_get_fd(&vdev->intx.interrupt);
    qemu_set_fd_handler(fd, vfio_intx_interrupt, NULL, vdev);


    if (cpr_is_incoming()) {
        if (kvm_enabled() && !vfio_cpr_intx_enable_kvm(vdev, &err)) {
            warn_reportf_err(err, VFIO_MSG_PREFIX, vdev->vbasedev.name);
        }
        goto skip_signaling;
    }

    if (!vfio_device_irq_set_signaling(&vdev->vbasedev, VFIO_PCI_INTX_IRQ_INDEX, 0,
                                VFIO_IRQ_SET_ACTION_TRIGGER, fd, errp)) {
        qemu_set_fd_handler(fd, NULL, NULL, vdev);
        vfio_notifier_cleanup(vdev, &vdev->intx.interrupt, "intx-interrupt", 0);
        return false;
    }

    if (kvm_enabled() && !vfio_intx_enable_kvm(vdev, &err)) {
        warn_reportf_err(err, VFIO_MSG_PREFIX, vdev->vbasedev.name);
    }

skip_signaling:
    vdev->interrupt = VFIO_INT_INTx;

    trace_vfio_intx_enable(vdev->vbasedev.name);
    return true;
}

void vfio_intx_disable(VFIOPCIDevice *vdev)
{
    PCIDevice *pdev = PCI_DEVICE(vdev);
    int fd;

    timer_del(vdev->intx.mmap_timer);
    if (kvm_enabled()) {
        vfio_intx_disable_kvm(vdev);
    }
    vfio_device_irq_disable(&vdev->vbasedev, VFIO_PCI_INTX_IRQ_INDEX);
    vdev->intx.pending = false;
    pci_irq_deassert(pdev);
    vfio_region_set_enabled(vdev, true);

    fd = event_notifier_get_fd(&vdev->intx.interrupt);
    qemu_set_fd_handler(fd, NULL, NULL, vdev);
    vfio_notifier_cleanup(vdev, &vdev->intx.interrupt, "intx-interrupt", 0);

    vdev->interrupt = VFIO_INT_NONE;

    trace_vfio_intx_disable(vdev->vbasedev.name);
}

bool vfio_pci_intx_enable(VFIOPCIDevice *vdev, Error **errp)
{
    return vfio_intx_enable(vdev, errp);
}

void vfio_pci_intx_set_handler(VFIOPCIDevice *vdev, bool enable)
{
    int fd = event_notifier_get_fd(&vdev->intx.interrupt);
    IOHandler *handler = (enable ? vfio_intx_interrupt : NULL);

    qemu_set_fd_handler(fd, handler, NULL, vdev);
}

/*
 * MSI/X
 */
static void vfio_msi_interrupt(void *opaque)
{
    VFIOMSIVector *vector = opaque;
    VFIOPCIDevice *vdev = vector->vdev;
    PCIDevice *pdev = PCI_DEVICE(vdev);
    MSIMessage (*get_msg)(PCIDevice *dev, unsigned vector);
    void (*notify)(PCIDevice *dev, unsigned vector);
    MSIMessage msg;
    int nr = vector - vdev->msi_vectors;

    if (!event_notifier_test_and_clear(&vector->interrupt)) {
        return;
    }

    if (vdev->interrupt == VFIO_INT_MSIX) {
        get_msg = msix_get_message;
        notify = msix_notify;

        /* A masked vector firing needs to use the PBA, enable it */
        if (msix_is_masked(pdev, nr)) {
            set_bit(nr, vdev->msix->pending);
            memory_region_set_enabled(&pdev->msix_pba_mmio, true);
            trace_vfio_msix_pba_enable(vdev->vbasedev.name);
        }
    } else if (vdev->interrupt == VFIO_INT_MSI) {
        get_msg = msi_get_message;
        notify = msi_notify;
    } else {
        /*
         * Interrupt state transitions (MSI/MSI-X -> NONE/INTx) are
         * protected by the BQL, and eventfd handlers are strictly
         * unregistered before vdev->interrupt is modified.
         */
        g_assert_not_reached();
    }

    msg = get_msg(pdev, nr);
    trace_vfio_msi_interrupt(vdev->vbasedev.name, nr, msg.address, msg.data);
    notify(pdev, nr);
}

void vfio_pci_msi_set_handler(VFIOPCIDevice *vdev, int nr, bool enable)
{
    VFIOMSIVector *vector = &vdev->msi_vectors[nr];
    int fd = event_notifier_get_fd(&vector->interrupt);
    IOHandler *handler = (enable ? vfio_msi_interrupt : NULL);

    qemu_set_fd_handler(fd, handler, NULL, vector);
}

/*
 * Get MSI-X enabled, but no vector enabled, by setting vector 0 with an invalid
 * fd to kernel.
 */
static int vfio_enable_msix_no_vec(VFIOPCIDevice *vdev)
{
    g_autofree struct vfio_irq_set *irq_set = NULL;
    int argsz;
    int32_t *fd;

    argsz = sizeof(*irq_set) + sizeof(*fd);

    irq_set = g_malloc0(argsz);
    irq_set->argsz = argsz;
    irq_set->flags = VFIO_IRQ_SET_DATA_EVENTFD |
                     VFIO_IRQ_SET_ACTION_TRIGGER;
    irq_set->index = VFIO_PCI_MSIX_IRQ_INDEX;
    irq_set->start = 0;
    irq_set->count = 1;
    fd = (int32_t *)&irq_set->data;
    *fd = -1;

    return vdev->vbasedev.io_ops->set_irqs(&vdev->vbasedev, irq_set);
}

static int vfio_enable_vectors(VFIOPCIDevice *vdev, bool msix)
{
    PCIDevice *pdev = PCI_DEVICE(vdev);
    struct vfio_irq_set *irq_set;
    int ret = 0, i, argsz;
    int32_t *fds;

    /*
     * If dynamic MSI-X allocation is supported, the vectors to be allocated
     * and enabled can be scattered. Before kernel enabling MSI-X, setting
     * nr_vectors causes all these vectors to be allocated on host.
     *
     * To keep allocation as needed, use vector 0 with an invalid fd to get
     * MSI-X enabled first, then set vectors with a potentially sparse set of
     * eventfds to enable interrupts only when enabled in guest.
     */
    if (msix && !vdev->msix->noresize) {
        ret = vfio_enable_msix_no_vec(vdev);

        if (ret) {
            return ret;
        }
    }

    argsz = sizeof(*irq_set) + (vdev->nr_vectors * sizeof(*fds));

    irq_set = g_malloc0(argsz);
    irq_set->argsz = argsz;
    irq_set->flags = VFIO_IRQ_SET_DATA_EVENTFD | VFIO_IRQ_SET_ACTION_TRIGGER;
    irq_set->index = msix ? VFIO_PCI_MSIX_IRQ_INDEX : VFIO_PCI_MSI_IRQ_INDEX;
    irq_set->start = 0;
    irq_set->count = vdev->nr_vectors;
    fds = (int32_t *)&irq_set->data;

    for (i = 0; i < vdev->nr_vectors; i++) {
        int fd = -1;

        /*
         * MSI vs MSI-X - The guest has direct access to MSI mask and pending
         * bits, therefore we always use the KVM signaling path when setup.
         * MSI-X mask and pending bits are emulated, so we want to use the
         * KVM signaling path only when configured and unmasked.
         */
        if (vdev->msi_vectors[i].use) {
            if (vdev->msi_vectors[i].virq < 0 ||
                (msix && msix_is_masked(pdev, i))) {
                fd = event_notifier_get_fd(&vdev->msi_vectors[i].interrupt);
            } else {
                fd = event_notifier_get_fd(&vdev->msi_vectors[i].kvm_interrupt);
            }
        }

        fds[i] = fd;
    }

    ret = vdev->vbasedev.io_ops->set_irqs(&vdev->vbasedev, irq_set);

    g_free(irq_set);

    return ret;
}

void vfio_pci_add_kvm_msi_virq(VFIOPCIDevice *vdev, VFIOMSIVector *vector,
                               int vector_n, bool msix)
{
    PCIDevice *pdev = PCI_DEVICE(vdev);

    if ((msix && vdev->no_kvm_msix) || (!msix && vdev->no_kvm_msi)) {
        return;
    }

    vector->virq = kvm_irqchip_add_msi_route(&vfio_route_change,
                                             vector_n, pdev);
}

static void vfio_connect_kvm_msi_virq(VFIOMSIVector *vector, int nr)
{
    const char *name = "kvm_interrupt";

    if (!vector->use || vector->virq < 0) {
        return;
    }

    if (!vfio_notifier_init(vector->vdev, &vector->kvm_interrupt, name, nr,
                            NULL)) {
        goto fail_notifier;
    }

    if (kvm_irqchip_add_irqfd_notifier_gsi(kvm_state, &vector->kvm_interrupt,
                                           NULL, vector->virq) < 0) {
        goto fail_kvm;
    }

    return;

fail_kvm:
    vfio_notifier_cleanup(vector->vdev, &vector->kvm_interrupt, name, nr);
fail_notifier:
    kvm_irqchip_release_virq(kvm_state, vector->virq);
    vector->virq = -1;
}

static void vfio_remove_kvm_msi_virq(VFIOPCIDevice *vdev, VFIOMSIVector *vector,
                                     int nr)
{
    kvm_irqchip_remove_irqfd_notifier_gsi(kvm_state, &vector->kvm_interrupt,
                                          vector->virq);
    kvm_irqchip_release_virq(kvm_state, vector->virq);
    vector->virq = -1;
    vfio_notifier_cleanup(vdev, &vector->kvm_interrupt, "kvm_interrupt", nr);
}

static void vfio_update_kvm_msi_virq(VFIOMSIVector *vector, MSIMessage msg,
                                     PCIDevice *pdev)
{
    kvm_irqchip_update_msi_route(kvm_state, vector->virq, msg, pdev);
    kvm_irqchip_commit_routes(kvm_state);
}

static void set_irq_signalling(VFIODevice *vbasedev, VFIOMSIVector *vector,
                               unsigned int nr)
{
    Error *err = NULL;
    int32_t fd;

    if (vector->virq >= 0) {
        fd = event_notifier_get_fd(&vector->kvm_interrupt);
    } else {
        fd = event_notifier_get_fd(&vector->interrupt);
    }

    if (!vfio_device_irq_set_signaling(vbasedev, VFIO_PCI_MSIX_IRQ_INDEX, nr,
                                       VFIO_IRQ_SET_ACTION_TRIGGER,
                                       fd, &err)) {
        error_reportf_err(err, VFIO_MSG_PREFIX, vbasedev->name);
    }
}

void vfio_pci_vector_init(VFIOPCIDevice *vdev, int nr)
{
    VFIOMSIVector *vector = &vdev->msi_vectors[nr];
    PCIDevice *pdev = PCI_DEVICE(vdev);
    Error *local_err = NULL;

    vector->vdev = vdev;
    vector->virq = -1;
    if (!vfio_notifier_init(vdev, &vector->interrupt, "interrupt", nr,
                            &local_err)) {
        error_report_err(local_err);
    }
    vector->use = true;
    if (vdev->interrupt == VFIO_INT_MSIX) {
        msix_vector_use(pdev, nr);
    }
}

static int vfio_msix_vector_do_use(PCIDevice *pdev, unsigned int nr,
                                   MSIMessage *msg, IOHandler *handler)
{
    VFIOPCIDevice *vdev = VFIO_PCI_DEVICE(pdev);
    VFIOMSIVector *vector;
    int ret;
    bool resizing = !!(vdev->nr_vectors < nr + 1);

    trace_vfio_msix_vector_do_use(vdev->vbasedev.name, nr);

    vector = &vdev->msi_vectors[nr];

    if (!vector->use) {
        vfio_pci_vector_init(vdev, nr);
    }

    qemu_set_fd_handler(event_notifier_get_fd(&vector->interrupt),
                        handler, NULL, vector);

    /*
     * Attempt to enable route through KVM irqchip,
     * default to userspace handling if unavailable.
     */
    if (vector->virq >= 0) {
        if (!msg) {
            vfio_remove_kvm_msi_virq(vdev, vector, nr);
        } else {
            vfio_update_kvm_msi_virq(vector, *msg, pdev);
        }
    } else {
        if (msg) {
            if (vdev->defer_kvm_irq_routing) {
                vfio_pci_add_kvm_msi_virq(vdev, vector, nr, true);
            } else if (accel_msi_via_irqfd_enabled()) {
                vfio_route_change = accel_irqchip_begin_route_changes();
                vfio_pci_add_kvm_msi_virq(vdev, vector, nr, true);
                accel_irqchip_commit_route_changes(&vfio_route_change);
                vfio_connect_kvm_msi_virq(vector, nr);
            }
        }
    }

    /*
     * When dynamic allocation is not supported, we don't want to have the
     * host allocate all possible MSI vectors for a device if they're not
     * in use, so we shutdown and incrementally increase them as needed.
     * nr_vectors represents the total number of vectors allocated.
     *
     * When dynamic allocation is supported, let the host only allocate
     * and enable a vector when it is in use in guest. nr_vectors represents
     * the upper bound of vectors being enabled (but not all of the ranges
     * is allocated or enabled).
     */
    if (resizing) {
        vdev->nr_vectors = nr + 1;
    }

    if (!vdev->defer_kvm_irq_routing) {
        if (vdev->msix->noresize && resizing) {
            vfio_device_irq_disable(&vdev->vbasedev, VFIO_PCI_MSIX_IRQ_INDEX);
            ret = vfio_enable_vectors(vdev, true);
            if (ret) {
                error_report("vfio: failed to enable vectors, %s",
                             strerror(-ret));
            }
        } else {
            set_irq_signalling(&vdev->vbasedev, vector, nr);
        }
    }

    /* Disable PBA emulation when nothing more is pending. */
    clear_bit(nr, vdev->msix->pending);
    if (find_first_bit(vdev->msix->pending,
                       vdev->nr_vectors) == vdev->nr_vectors) {
        memory_region_set_enabled(&pdev->msix_pba_mmio, false);
        trace_vfio_msix_pba_disable(vdev->vbasedev.name);
    }

    return 0;
}

static int vfio_msix_vector_use(PCIDevice *pdev,
                                unsigned int nr, MSIMessage msg)
{
    /*
     * Ignore the callback from msix_set_vector_notifiers during resume.
     * The necessary subset of these actions is called from
     * vfio_cpr_claim_vectors during post load.
     */
    if (cpr_is_incoming()) {
        return 0;
    }

    return vfio_msix_vector_do_use(pdev, nr, &msg, vfio_msi_interrupt);
}

static void vfio_msix_vector_release(PCIDevice *pdev, unsigned int nr)
{
    VFIOPCIDevice *vdev = VFIO_PCI_DEVICE(pdev);
    VFIOMSIVector *vector = &vdev->msi_vectors[nr];

    trace_vfio_msix_vector_release(vdev->vbasedev.name, nr);

    /*
     * There are still old guests that mask and unmask vectors on every
     * interrupt.  If we're using QEMU bypass with a KVM irqfd, leave all of
     * the KVM setup in place, simply switch VFIO to use the non-bypass
     * eventfd.  We'll then fire the interrupt through QEMU and the MSI-X
     * core will mask the interrupt and set pending bits, allowing it to
     * be re-asserted on unmask.  Nothing to do if already using QEMU mode.
     */
    if (vector->virq >= 0) {
        int32_t fd = event_notifier_get_fd(&vector->interrupt);
        Error *err = NULL;

        if (!vfio_device_irq_set_signaling(&vdev->vbasedev, VFIO_PCI_MSIX_IRQ_INDEX,
                                    nr, VFIO_IRQ_SET_ACTION_TRIGGER, fd,
                                    &err)) {
            error_reportf_err(err, VFIO_MSG_PREFIX, vdev->vbasedev.name);
        }
    }
}

void vfio_pci_msix_set_notifiers(VFIOPCIDevice *vdev)
{
    PCIDevice *pdev = PCI_DEVICE(vdev);

    msix_set_vector_notifiers(pdev, vfio_msix_vector_use,
                              vfio_msix_vector_release, NULL);
}

void vfio_pci_prepare_kvm_msi_virq_batch(VFIOPCIDevice *vdev)
{
    assert(!vdev->defer_kvm_irq_routing);
    vdev->defer_kvm_irq_routing = true;
    if (accel_msi_via_irqfd_enabled()) {
        vfio_route_change = accel_irqchip_begin_route_changes();
    }
}

void vfio_pci_commit_kvm_msi_virq_batch(VFIOPCIDevice *vdev)
{
    int i;

    assert(vdev->defer_kvm_irq_routing);
    vdev->defer_kvm_irq_routing = false;

    accel_irqchip_commit_route_changes(&vfio_route_change);

    for (i = 0; i < vdev->nr_vectors; i++) {
        vfio_connect_kvm_msi_virq(&vdev->msi_vectors[i], i);
    }
}

void vfio_msix_enable(VFIOPCIDevice *vdev)
{
    PCIDevice *pdev = PCI_DEVICE(vdev);
    int ret;

    vfio_disable_interrupts(vdev);

    vdev->msi_vectors = g_new0(VFIOMSIVector, vdev->msix->entries);

    vdev->interrupt = VFIO_INT_MSIX;

    /*
     * Setting vector notifiers triggers synchronous vector-use
     * callbacks for each active vector.  Deferring to commit the KVM
     * routes once rather than per vector provides a substantial
     * performance improvement.
     */
    vfio_pci_prepare_kvm_msi_virq_batch(vdev);

    if (msix_set_vector_notifiers(pdev, vfio_msix_vector_use,
                                  vfio_msix_vector_release, NULL)) {
        error_report("vfio: msix_set_vector_notifiers failed");
    }

    vfio_pci_commit_kvm_msi_virq_batch(vdev);

    if (vdev->nr_vectors) {
        ret = vfio_enable_vectors(vdev, true);
        if (ret) {
            error_report("vfio: failed to enable vectors, %s",
                         strerror(-ret));
        }
    } else {
        /*
         * Some communication channels between VF & PF or PF & fw rely on the
         * physical state of the device and expect that enabling MSI-X from the
         * guest enables the same on the host.  When our guest is Linux, the
         * guest driver call to pci_enable_msix() sets the enabling bit in the
         * MSI-X capability, but leaves the vector table masked.  We therefore
         * can't rely on a vector_use callback (from request_irq() in the guest)
         * to switch the physical device into MSI-X mode because that may come a
         * long time after pci_enable_msix().  This code sets vector 0 with an
         * invalid fd to make the physical device MSI-X enabled, but with no
         * vectors enabled, just like the guest view.
         */
        ret = vfio_enable_msix_no_vec(vdev);
        if (ret) {
            error_report("vfio: failed to enable MSI-X, %s",
                         strerror(-ret));
        }
    }

    trace_vfio_msix_enable(vdev->vbasedev.name);
}

void vfio_msi_enable(VFIOPCIDevice *vdev)
{
    PCIDevice *pdev = PCI_DEVICE(vdev);
    int ret, i;

    vfio_disable_interrupts(vdev);

    vdev->nr_vectors = msi_nr_vectors_allocated(pdev);
retry:
    /*
     * Setting vector notifiers needs to enable route for each vector.
     * Deferring to commit the KVM routes once rather than per vector
     * provides a substantial performance improvement.
     */
    vfio_pci_prepare_kvm_msi_virq_batch(vdev);

    vdev->msi_vectors = g_new0(VFIOMSIVector, vdev->nr_vectors);

    for (i = 0; i < vdev->nr_vectors; i++) {
        VFIOMSIVector *vector = &vdev->msi_vectors[i];
        Error *local_err = NULL;

        vector->vdev = vdev;
        vector->virq = -1;
        vector->use = true;

        if (!vfio_notifier_init(vdev, &vector->interrupt, "interrupt", i,
                                &local_err)) {
            error_report_err(local_err);
        }

        qemu_set_fd_handler(event_notifier_get_fd(&vector->interrupt),
                            vfio_msi_interrupt, NULL, vector);

        /*
         * Attempt to enable route through KVM irqchip,
         * default to userspace handling if unavailable.
         */
        vfio_pci_add_kvm_msi_virq(vdev, vector, i, false);
    }

    vfio_pci_commit_kvm_msi_virq_batch(vdev);

    /* Set interrupt type prior to possible interrupts */
    vdev->interrupt = VFIO_INT_MSI;

    ret = vfio_enable_vectors(vdev, false);
    if (ret) {
        if (ret < 0) {
            error_report("vfio: Error: Failed to setup MSI fds: %s",
                         strerror(-ret));
        } else {
            error_report("vfio: Error: Failed to enable %d "
                         "MSI vectors, retry with %d", vdev->nr_vectors, ret);
        }

        vfio_msi_disable_common(vdev);

        if (ret > 0) {
            vdev->nr_vectors = ret;
            goto retry;
        }

        /*
         * Failing to setup MSI doesn't really fall within any specification.
         * Let's try leaving interrupts disabled and hope the guest figures
         * out to fall back to INTx for this device.
         */
        error_report("vfio: Error: Failed to enable MSI");

        return;
    }

    trace_vfio_msi_enable(vdev->vbasedev.name, vdev->nr_vectors);
}

static void vfio_msi_disable_common(VFIOPCIDevice *vdev)
{
    int i;

    for (i = 0; i < vdev->nr_vectors; i++) {
        VFIOMSIVector *vector = &vdev->msi_vectors[i];
        if (vdev->msi_vectors[i].use) {
            if (vector->virq >= 0) {
                vfio_remove_kvm_msi_virq(vdev, vector, i);
            }
            qemu_set_fd_handler(event_notifier_get_fd(&vector->interrupt),
                                NULL, NULL, NULL);
            vfio_notifier_cleanup(vdev, &vector->interrupt, "interrupt", i);
        }
    }

    g_free(vdev->msi_vectors);
    vdev->msi_vectors = NULL;
    vdev->nr_vectors = 0;
    vdev->interrupt = VFIO_INT_NONE;
}

void vfio_msix_disable(VFIOPCIDevice *vdev)
{
    PCIDevice *pdev = PCI_DEVICE(vdev);
    Error *err = NULL;
    int i;

    msix_unset_vector_notifiers(pdev);

    /*
     * MSI-X will only release vectors if MSI-X is still enabled on the
     * device, check through the rest and release it ourselves if necessary.
     */
    for (i = 0; i < vdev->nr_vectors; i++) {
        if (vdev->msi_vectors[i].use) {
            vfio_msix_vector_release(pdev, i);
            msix_vector_unuse(pdev, i);
        }
    }

    /*
     * Always clear MSI-X IRQ index. A PF device could have enabled
     * MSI-X with no vectors. See vfio_msix_enable().
     */
    vfio_device_irq_disable(&vdev->vbasedev, VFIO_PCI_MSIX_IRQ_INDEX);

    vfio_msi_disable_common(vdev);
    if (!vfio_intx_enable(vdev, &err)) {
        error_reportf_err(err, VFIO_MSG_PREFIX, vdev->vbasedev.name);
    }

    memset(vdev->msix->pending, 0,
           BITS_TO_LONGS(vdev->msix->entries) * sizeof(unsigned long));

    trace_vfio_msix_disable(vdev->vbasedev.name);
}

void vfio_msi_disable(VFIOPCIDevice *vdev)
{
    Error *err = NULL;

    vfio_device_irq_disable(&vdev->vbasedev, VFIO_PCI_MSI_IRQ_INDEX);
    vfio_msi_disable_common(vdev);
    vfio_intx_enable(vdev, &err);
    if (err) {
        error_reportf_err(err, VFIO_MSG_PREFIX, vdev->vbasedev.name);
    }

    trace_vfio_msi_disable(vdev->vbasedev.name);
}

void vfio_update_msi(VFIOPCIDevice *vdev)
{
    PCIDevice *pdev = PCI_DEVICE(vdev);
    int i;

    for (i = 0; i < vdev->nr_vectors; i++) {
        VFIOMSIVector *vector = &vdev->msi_vectors[i];
        MSIMessage msg;

        if (!vector->use || vector->virq < 0) {
            continue;
        }

        msg = msi_get_message(pdev, i);
        vfio_update_kvm_msi_virq(vector, msg, pdev);
    }
}

/*
 * Interrupt setup
 */
void vfio_disable_interrupts(VFIOPCIDevice *vdev)
{
    /*
     * More complicated than it looks.  Disabling MSI/X transitions the
     * device to INTx mode (if supported).  Therefore we need to first
     * disable MSI/X and then cleanup by disabling INTx.
     */
    if (vdev->interrupt == VFIO_INT_MSIX) {
        vfio_msix_disable(vdev);
    } else if (vdev->interrupt == VFIO_INT_MSI) {
        vfio_msi_disable(vdev);
    }

    if (vdev->interrupt == VFIO_INT_INTx) {
        vfio_intx_disable(vdev);
    }
}

bool vfio_msi_setup(VFIOPCIDevice *vdev, int pos, Error **errp)
{
    PCIDevice *pdev = PCI_DEVICE(vdev);
    uint16_t ctrl;
    bool msi_64bit, msi_maskbit;
    int ret, entries;
    Error *err = NULL;

    ret = vfio_pci_config_space_read(vdev, pos + PCI_CAP_FLAGS,
                                     sizeof(ctrl), &ctrl);
    if (ret != sizeof(ctrl)) {
        error_setg(errp, "failed reading MSI PCI_CAP_FLAGS: %s",
                   strreaderror(ret));
        return false;
    }
    ctrl = le16_to_cpu(ctrl);

    msi_64bit = !!(ctrl & PCI_MSI_FLAGS_64BIT);
    msi_maskbit = !!(ctrl & PCI_MSI_FLAGS_MASKBIT);
    entries = 1 << ((ctrl & PCI_MSI_FLAGS_QMASK) >> 1);

    trace_vfio_msi_setup(vdev->vbasedev.name, pos);

    ret = msi_init(pdev, pos, entries, msi_64bit, msi_maskbit, &err);
    if (ret < 0) {
        if (ret == -ENOTSUP) {
            return true;
        }
        error_propagate_prepend(errp, err, "msi_init failed: ");
        return false;
    }
    vdev->msi_cap_size = 0xa + (msi_maskbit ? 0xa : 0) + (msi_64bit ? 0x4 : 0);

    return true;
}

static void vfio_pci_fixup_msix_region(VFIOPCIDevice *vdev)
{
    off_t start, end;
    VFIORegion *region = &vdev->bars[vdev->msix->table_bar].region;

    /*
     * If the host driver allows mapping of a MSIX data, we are going to
     * do map the entire BAR and emulate MSIX table on top of that.
     */
    if (vfio_device_has_region_cap(&vdev->vbasedev, region->nr,
                                   VFIO_REGION_INFO_CAP_MSIX_MAPPABLE)) {
        return;
    }

    /*
     * We expect to find a single mmap covering the whole BAR, anything else
     * means it's either unsupported or already setup.
     */
    if (region->nr_mmaps != 1 || region->mmaps[0].offset ||
        region->size != region->mmaps[0].size) {
        return;
    }

    /* MSI-X table start and end aligned to host page size */
    start = vdev->msix->table_offset & qemu_real_host_page_mask();
    end = REAL_HOST_PAGE_ALIGN((uint64_t)vdev->msix->table_offset +
                               (vdev->msix->entries * PCI_MSIX_ENTRY_SIZE));

    /*
     * Does the MSI-X table cover the beginning of the BAR?  The whole BAR?
     * NB - Host page size is necessarily a power of two and so is the PCI
     * BAR (not counting EA yet), therefore if we have host page aligned
     * @start and @end, then any remainder of the BAR before or after those
     * must be at least host page sized and therefore mmap'able.
     */
    if (!start) {
        if (end >= region->size) {
            region->nr_mmaps = 0;
            g_free(region->mmaps);
            region->mmaps = NULL;
            trace_vfio_msix_fixup(vdev->vbasedev.name,
                                  vdev->msix->table_bar, 0, 0);
        } else {
            region->mmaps[0].offset = end;
            region->mmaps[0].fd_offset += end;
            region->mmaps[0].size = region->size - end;
            trace_vfio_msix_fixup(vdev->vbasedev.name,
                              vdev->msix->table_bar, region->mmaps[0].offset,
                              region->mmaps[0].offset + region->mmaps[0].size);
        }

    /* Maybe it's aligned at the end of the BAR */
    } else if (end >= region->size) {
        region->mmaps[0].size = start;
        trace_vfio_msix_fixup(vdev->vbasedev.name,
                              vdev->msix->table_bar, region->mmaps[0].offset,
                              region->mmaps[0].offset + region->mmaps[0].size);

    /* Otherwise it must split the BAR */
    } else {
        region->nr_mmaps = 2;
        region->mmaps = g_renew(VFIOMmap, region->mmaps, 2);

        memcpy(&region->mmaps[1], &region->mmaps[0], sizeof(VFIOMmap));

        region->mmaps[0].size = start;
        trace_vfio_msix_fixup(vdev->vbasedev.name,
                              vdev->msix->table_bar, region->mmaps[0].offset,
                              region->mmaps[0].offset + region->mmaps[0].size);

        region->mmaps[1].offset = end;
        region->mmaps[1].fd_offset += end;
        region->mmaps[1].size = region->size - end;
        trace_vfio_msix_fixup(vdev->vbasedev.name,
                              vdev->msix->table_bar, region->mmaps[1].offset,
                              region->mmaps[1].offset + region->mmaps[1].size);
    }
}

static bool vfio_pci_relocate_msix(VFIOPCIDevice *vdev, Error **errp)
{
    int target_bar = -1;
    size_t msix_sz;

    if (!vdev->msix || vdev->msix_relo == OFF_AUTO_PCIBAR_OFF) {
        return true;
    }

    /* The actual minimum size of MSI-X structures */
    msix_sz = (vdev->msix->entries * PCI_MSIX_ENTRY_SIZE) +
              (QEMU_ALIGN_UP(vdev->msix->entries, 64) / 8);
    /* Round up to host pages, we don't want to share a page */
    msix_sz = REAL_HOST_PAGE_ALIGN(msix_sz);
    /* PCI BARs must be a power of 2 */
    msix_sz = pow2ceil(msix_sz);

    if (vdev->msix_relo == OFF_AUTO_PCIBAR_AUTO) {
        /*
         * TODO: Lookup table for known devices.
         *
         * Logically we might use an algorithm here to select the BAR adding
         * the least additional MMIO space, but we cannot programmatically
         * predict the driver dependency on BAR ordering or sizing, therefore
         * 'auto' becomes a lookup for combinations reported to work.
         */
        if (target_bar < 0) {
            error_setg(errp, "No automatic MSI-X relocation available for "
                       "device %04x:%04x", vdev->vendor_id, vdev->device_id);
            return false;
        }
    } else {
        target_bar = (int)(vdev->msix_relo - OFF_AUTO_PCIBAR_BAR0);
    }

    /* I/O port BARs cannot host MSI-X structures */
    if (vdev->bars[target_bar].ioport) {
        error_setg(errp, "Invalid MSI-X relocation BAR %d, "
                   "I/O port BAR", target_bar);
        return false;
    }

    /* Cannot use a BAR in the "shadow" of a 64-bit BAR */
    if (!vdev->bars[target_bar].size &&
         target_bar > 0 && vdev->bars[target_bar - 1].mem64) {
        error_setg(errp, "Invalid MSI-X relocation BAR %d, "
                   "consumed by 64-bit BAR %d", target_bar, target_bar - 1);
        return false;
    }

    /* 2GB max size for 32-bit BARs, cannot double if already > 1G */
    if (vdev->bars[target_bar].size > 1 * GiB &&
        !vdev->bars[target_bar].mem64) {
        error_setg(errp, "Invalid MSI-X relocation BAR %d, "
                   "no space to extend 32-bit BAR", target_bar);
        return false;
    }

    /*
     * If adding a new BAR, test if we can make it 64bit.  We make it
     * prefetchable since QEMU MSI-X emulation has no read side effects
     * and doing so makes mapping more flexible.
     */
    if (!vdev->bars[target_bar].size) {
        if (target_bar < (PCI_ROM_SLOT - 1) &&
            !vdev->bars[target_bar + 1].size) {
            vdev->bars[target_bar].mem64 = true;
            vdev->bars[target_bar].type = PCI_BASE_ADDRESS_MEM_TYPE_64;
        }
        vdev->bars[target_bar].type |= PCI_BASE_ADDRESS_MEM_PREFETCH;
        vdev->bars[target_bar].size = msix_sz;
        vdev->msix->table_offset = 0;
    } else {
        vdev->bars[target_bar].size = MAX(vdev->bars[target_bar].size * 2,
                                          msix_sz * 2);
        /*
         * Due to above size calc, MSI-X always starts halfway into the BAR,
         * which will always be a separate host page.
         */
        vdev->msix->table_offset = vdev->bars[target_bar].size / 2;
    }

    vdev->msix->table_bar = target_bar;
    vdev->msix->pba_bar = target_bar;
    /* Requires 8-byte alignment, but PCI_MSIX_ENTRY_SIZE guarantees that */
    vdev->msix->pba_offset = vdev->msix->table_offset +
                                  (vdev->msix->entries * PCI_MSIX_ENTRY_SIZE);

    trace_vfio_msix_relo(vdev->vbasedev.name,
                         vdev->msix->table_bar, vdev->msix->table_offset);
    return true;
}

/*
 * We don't have any control over how pci_add_capability() inserts
 * capabilities into the chain.  In order to setup MSI-X we need a
 * MemoryRegion for the BAR.  In order to setup the BAR and not
 * attempt to mmap the MSI-X table area, which VFIO won't allow, we
 * need to first look for where the MSI-X table lives.  So we
 * unfortunately split MSI-X setup across two functions.
 */
bool vfio_msix_early_setup(VFIOPCIDevice *vdev, Error **errp)
{
    PCIDevice *pdev = PCI_DEVICE(vdev);
    uint8_t pos;
    uint16_t ctrl;
    uint32_t table, pba;
    struct vfio_irq_info irq_info;
    VFIOMSIXInfo *msix;
    int ret;

    pos = pci_find_capability(pdev, PCI_CAP_ID_MSIX);
    if (!pos) {
        return true;
    }

    ret = vfio_pci_config_space_read(vdev, pos + PCI_MSIX_FLAGS,
                                     sizeof(ctrl), &ctrl);
    if (ret != sizeof(ctrl)) {
        error_setg(errp, "failed to read PCI MSIX FLAGS: %s",
                   strreaderror(ret));
        return false;
    }

    ret = vfio_pci_config_space_read(vdev, pos + PCI_MSIX_TABLE,
                                     sizeof(table), &table);
    if (ret != sizeof(table)) {
        error_setg(errp, "failed to read PCI MSIX TABLE: %s",
                   strreaderror(ret));
        return false;
    }

    ret = vfio_pci_config_space_read(vdev, pos + PCI_MSIX_PBA,
                                     sizeof(pba), &pba);
    if (ret != sizeof(pba)) {
        error_setg(errp, "failed to read PCI MSIX PBA: %s", strreaderror(ret));
        return false;
    }

    ctrl = le16_to_cpu(ctrl);
    table = le32_to_cpu(table);
    pba = le32_to_cpu(pba);

    msix = g_malloc0(sizeof(*msix));
    msix->table_bar = table & PCI_MSIX_FLAGS_BIRMASK;
    msix->table_offset = table & ~PCI_MSIX_FLAGS_BIRMASK;
    msix->pba_bar = pba & PCI_MSIX_FLAGS_BIRMASK;
    msix->pba_offset = pba & ~PCI_MSIX_FLAGS_BIRMASK;
    msix->entries = (ctrl & PCI_MSIX_FLAGS_QSIZE) + 1;

    if (msix->table_bar >= ARRAY_SIZE(vdev->bars) ||
        msix->pba_bar >= ARRAY_SIZE(vdev->bars)) {
        error_setg(errp, "invalid MSI-X BIR, table_bar=%d pba_bar=%d",
                   msix->table_bar, msix->pba_bar);
        g_free(msix);
        return false;
    }

    ret = vfio_device_get_irq_info(&vdev->vbasedev, VFIO_PCI_MSIX_IRQ_INDEX,
                                   &irq_info);
    if (ret < 0) {
        error_setg_errno(errp, -ret, "failed to get MSI-X irq info");
        g_free(msix);
        return false;
    }

    msix->noresize = !!(irq_info.flags & VFIO_IRQ_INFO_NORESIZE);

    /*
     * Test the size of the pba_offset variable and catch if it extends outside
     * of the specified BAR. If it is the case, we need to apply a hardware
     * specific quirk if the device is known or we have a broken configuration.
     */
    if (msix->pba_offset >= vdev->bars[msix->pba_bar].region.size) {
        /*
         * Chelsio T5 Virtual Function devices are encoded as 0x58xx for T5
         * adapters. The T5 hardware returns an incorrect value of 0x8000 for
         * the VF PBA offset while the BAR itself is only 8k. The correct value
         * is 0x1000, so we hard code that here.
         */
        if (vdev->vendor_id == PCI_VENDOR_ID_CHELSIO &&
            (vdev->device_id & 0xff00) == 0x5800) {
            msix->pba_offset = 0x1000;
        /*
         * BAIDU KUNLUN Virtual Function devices for KUNLUN AI processor
         * return an incorrect value of 0x460000 for the VF PBA offset while
         * the BAR itself is only 0x10000.  The correct value is 0xb400.
         */
        } else if (vfio_pci_is(vdev, PCI_VENDOR_ID_BAIDU,
                               PCI_DEVICE_ID_KUNLUN_VF)) {
            msix->pba_offset = 0xb400;
        } else if (vdev->msix_relo == OFF_AUTO_PCIBAR_OFF) {
            error_setg(errp, "hardware reports invalid configuration, "
                       "MSIX PBA outside of specified BAR");
            g_free(msix);
            return false;
        }
    }

    trace_vfio_msix_early_setup(vdev->vbasedev.name, pos, msix->table_bar,
                                msix->table_offset, msix->entries,
                                msix->noresize);
    vdev->msix = msix;

    vfio_pci_fixup_msix_region(vdev);

    return vfio_pci_relocate_msix(vdev, errp);
}

bool vfio_msix_setup(VFIOPCIDevice *vdev, int pos, Error **errp)
{
    PCIDevice *pdev = PCI_DEVICE(vdev);
    int ret;
    Error *err = NULL;

    vdev->msix->pending = g_new0(unsigned long,
                                 BITS_TO_LONGS(vdev->msix->entries));
    ret = msix_init(pdev, vdev->msix->entries,
                    vdev->bars[vdev->msix->table_bar].mr,
                    vdev->msix->table_bar, vdev->msix->table_offset,
                    vdev->bars[vdev->msix->pba_bar].mr,
                    vdev->msix->pba_bar, vdev->msix->pba_offset, pos,
                    &err);
    if (ret < 0) {
        if (ret == -ENOTSUP) {
            warn_report_err(err);
            return true;
        }

        error_propagate(errp, err);
        return false;
    }

    /*
     * The PCI spec suggests that devices provide additional alignment for
     * MSI-X structures and avoid overlapping non-MSI-X related registers.
     * For an assigned device, this hopefully means that emulation of MSI-X
     * structures does not affect the performance of the device.  If devices
     * fail to provide that alignment, a significant performance penalty may
     * result, for instance Mellanox MT27500 VFs:
     * http://www.spinics.net/lists/kvm/msg125881.html
     *
     * The PBA is simply not that important for such a serious regression and
     * most drivers do not appear to look at it.  The solution for this is to
     * disable the PBA MemoryRegion unless it's being used.  We disable it
     * here and only enable it if a masked vector fires through QEMU.  As the
     * vector-use notifier is called, which occurs on unmask, we test whether
     * PBA emulation is needed and again disable if not.
     */
    memory_region_set_enabled(&pdev->msix_pba_mmio, false);

    /*
     * The emulated machine may provide a paravirt interface for MSIX setup
     * so it is not strictly necessary to emulate MSIX here. This becomes
     * helpful when frequently accessed MMIO registers are located in
     * subpages adjacent to the MSIX table but the MSIX data containing page
     * cannot be mapped because of a host page size bigger than the MSIX table
     * alignment.
     */
    if (object_property_get_bool(OBJECT(qdev_get_machine()),
                                 "vfio-no-msix-emulation", NULL)) {
        memory_region_set_enabled(&pdev->msix_table_mmio, false);
    }

    return true;
}

void vfio_pci_teardown_msi(VFIOPCIDevice *vdev)
{
    PCIDevice *pdev = PCI_DEVICE(vdev);

    msi_uninit(pdev);

    if (vdev->msix) {
        msix_uninit(pdev,
                    vdev->bars[vdev->msix->table_bar].mr,
                    vdev->bars[vdev->msix->pba_bar].mr);
        g_free(vdev->msix->pending);
    }
}

/*
 * Err/Req notifiers
 */
static void vfio_err_notifier_handler(void *opaque)
{
    VFIOPCIDevice *vdev = opaque;

    if (!event_notifier_test_and_clear(&vdev->err_notifier)) {
        return;
    }

    /*
     * TBD. Retrieve the error details and decide what action
     * needs to be taken. One of the actions could be to pass
     * the error to the guest and have the guest driver recover
     * from the error. This requires that PCIe capabilities be
     * exposed to the guest. For now, we just terminate the
     * guest to contain the error.
     */

    error_report("%s(%s) Unrecoverable error detected. Please collect any data possible and then kill the guest", __func__, vdev->vbasedev.name);

    vm_stop(RUN_STATE_INTERNAL_ERROR);
}

/*
 * Registers error notifier for devices supporting error recovery.
 * If we encounter a failure in this function, we report an error
 * and continue after disabling error recovery support for the
 * device.
 */
void vfio_pci_register_err_notifier(VFIOPCIDevice *vdev)
{
    Error *err = NULL;
    int32_t fd;

    if (!vdev->pci_aer) {
        return;
    }

    if (!vfio_notifier_init(vdev, &vdev->err_notifier, "err_notifier", 0,
                            &err)) {
        error_report_err(err);
        vdev->pci_aer = false;
        return;
    }

    fd = event_notifier_get_fd(&vdev->err_notifier);
    qemu_set_fd_handler(fd, vfio_err_notifier_handler, NULL, vdev);

    /* Do not alter irq_signaling during vfio_realize for cpr */
    if (cpr_is_incoming()) {
        return;
    }

    if (!vfio_device_irq_set_signaling(&vdev->vbasedev, VFIO_PCI_ERR_IRQ_INDEX, 0,
                                       VFIO_IRQ_SET_ACTION_TRIGGER, fd, &err)) {
        error_reportf_err(err, VFIO_MSG_PREFIX, vdev->vbasedev.name);
        qemu_set_fd_handler(fd, NULL, NULL, vdev);
        vfio_notifier_cleanup(vdev, &vdev->err_notifier, "err_notifier", 0);
        vdev->pci_aer = false;
    }
}

void vfio_unregister_err_notifier(VFIOPCIDevice *vdev)
{
    Error *err = NULL;

    if (!vdev->pci_aer) {
        return;
    }

    if (!vfio_device_irq_set_signaling(&vdev->vbasedev, VFIO_PCI_ERR_IRQ_INDEX, 0,
                                       VFIO_IRQ_SET_ACTION_TRIGGER, -1, &err)) {
        error_reportf_err(err, VFIO_MSG_PREFIX, vdev->vbasedev.name);
    }
    qemu_set_fd_handler(event_notifier_get_fd(&vdev->err_notifier),
                        NULL, NULL, vdev);
    vfio_notifier_cleanup(vdev, &vdev->err_notifier, "err_notifier", 0);
}

static void vfio_req_notifier_handler(void *opaque)
{
    VFIOPCIDevice *vdev = opaque;
    Error *err = NULL;

    if (!event_notifier_test_and_clear(&vdev->req_notifier)) {
        return;
    }

    qdev_unplug(DEVICE(vdev), &err);
    if (err) {
        warn_reportf_err(err, VFIO_MSG_PREFIX, vdev->vbasedev.name);
    }
}

void vfio_pci_register_req_notifier(VFIOPCIDevice *vdev)
{
    struct vfio_irq_info irq_info;
    Error *err = NULL;
    int32_t fd;
    int ret;

    if (!(vdev->features & VFIO_FEATURE_ENABLE_REQ)) {
        return;
    }

    ret = vfio_device_get_irq_info(&vdev->vbasedev, VFIO_PCI_REQ_IRQ_INDEX,
                                   &irq_info);
    if (ret < 0 || irq_info.count < 1) {
        return;
    }

    if (!vfio_notifier_init(vdev, &vdev->req_notifier, "req_notifier", 0,
                            &err)) {
        error_report_err(err);
        return;
    }

    fd = event_notifier_get_fd(&vdev->req_notifier);
    qemu_set_fd_handler(fd, vfio_req_notifier_handler, NULL, vdev);

    /* Do not alter irq_signaling during vfio_realize for cpr */
    if (cpr_is_incoming()) {
        vdev->req_enabled = true;
        return;
    }

    if (!vfio_device_irq_set_signaling(&vdev->vbasedev, VFIO_PCI_REQ_IRQ_INDEX, 0,
                                       VFIO_IRQ_SET_ACTION_TRIGGER, fd, &err)) {
        error_reportf_err(err, VFIO_MSG_PREFIX, vdev->vbasedev.name);
        qemu_set_fd_handler(fd, NULL, NULL, vdev);
        vfio_notifier_cleanup(vdev, &vdev->req_notifier, "req_notifier", 0);
    } else {
        vdev->req_enabled = true;
    }
}

void vfio_unregister_req_notifier(VFIOPCIDevice *vdev)
{
    Error *err = NULL;

    if (!vdev->req_enabled) {
        return;
    }

    if (!vfio_device_irq_set_signaling(&vdev->vbasedev, VFIO_PCI_REQ_IRQ_INDEX, 0,
                                       VFIO_IRQ_SET_ACTION_TRIGGER, -1, &err)) {
        error_reportf_err(err, VFIO_MSG_PREFIX, vdev->vbasedev.name);
    }
    qemu_set_fd_handler(event_notifier_get_fd(&vdev->req_notifier),
                        NULL, NULL, vdev);
    vfio_notifier_cleanup(vdev, &vdev->req_notifier, "req_notifier", 0);

    vdev->req_enabled = false;
}

bool vfio_pci_interrupt_setup(VFIOPCIDevice *vdev, Error **errp)
{
    PCIDevice *pdev = PCI_DEVICE(vdev);

    /* QEMU emulates all of MSI & MSIX */
    if (pdev->cap_present & QEMU_PCI_CAP_MSIX) {
        memset(vdev->emulated_config_bits + pdev->msix_cap, 0xff,
               MSIX_CAP_LENGTH);
    }

    if (pdev->cap_present & QEMU_PCI_CAP_MSI) {
        memset(vdev->emulated_config_bits + pdev->msi_cap, 0xff,
               vdev->msi_cap_size);
    }

    if (vfio_pci_read_config(pdev, PCI_INTERRUPT_PIN, 1)) {
        vdev->intx.mmap_timer = timer_new_ms(QEMU_CLOCK_VIRTUAL,
                                             vfio_intx_mmap_enable, vdev);
        pci_device_set_intx_routing_notifier(pdev,
                                             vfio_intx_routing_notifier);
        vdev->irqchip_change_notifier.notify = vfio_irqchip_change;
        kvm_irqchip_add_change_notifier(&vdev->irqchip_change_notifier);

        /*
         * During CPR, do not call vfio_intx_enable at this time.  Instead,
         * call it from vfio_pci_post_load after the intx routing data has
         * been loaded from vmstate.
         */
        if (!cpr_is_incoming() && !vfio_intx_enable(vdev, errp)) {
            timer_free(vdev->intx.mmap_timer);
            pci_device_set_intx_routing_notifier(pdev, NULL);
            kvm_irqchip_remove_change_notifier(&vdev->irqchip_change_notifier);
            return false;
        }
    }
    return true;
}
