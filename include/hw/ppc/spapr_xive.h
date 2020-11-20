/*
 * QEMU PowerPC sPAPR XIVE interrupt controller model
 *
 * Copyright (c) 2017-2018, IBM Corporation.
 *
 * This code is licensed under the GPL version 2 or later. See the
 * COPYING file in the top-level directory.
 */

#ifndef PPC_SPAPR_XIVE_H
#define PPC_SPAPR_XIVE_H

#include "hw/ppc/spapr_irq.h"
#include "hw/ppc/xive.h"

#define TYPE_SPAPR_XIVE "spapr-xive"
#define SPAPR_XIVE(obj) OBJECT_CHECK(SpaprXive, (obj), TYPE_SPAPR_XIVE)
#define SPAPR_XIVE_CLASS(klass)                                         \
    OBJECT_CLASS_CHECK(SpaprXiveClass, (klass), TYPE_SPAPR_XIVE)
#define SPAPR_XIVE_GET_CLASS(obj)                               \
    OBJECT_GET_CLASS(SpaprXiveClass, (obj), TYPE_SPAPR_XIVE)

typedef struct SpaprXive {
    XiveRouter    parent;

    /*
     * The XIVE device needs to know the highest vCPU id it might
     * be exposed to in order to size the END table. It may also
     * propagate the value to the KVM XIVE device in order to
     * optimize resource allocation in the HW.
     * This must be set to a non-null value using the "nr-servers"
     * property, before realizing the device.
     */
    uint32_t      nr_servers;

    /* Internal interrupt source for IPIs and virtual devices */
    XiveSource    source;
    hwaddr        vc_base;

    /* END ESB MMIOs */
    XiveENDSource end_source;
    hwaddr        end_base;

    /* DT */
    gchar *nodename;
    /*
     * The sPAPR XIVE device needs to know how many vCPUs it
     * might be exposed to in order to size the IPI range in
     * "ibm,interrupt-server-ranges". It is the purpose of the
     * "nr-ipis" property which *must* be set to a non-null
     * value before realizing the sPAPR XIVE device.
     */
    uint32_t nr_ipis;

    /* Routing table */
    XiveEAS       *eat;
    uint32_t      nr_irqs;
    XiveEND       *endt;
    /*
     * This is derived from nr_servers but it must be kept around because
     * vmstate_spapr_xive uses it.
     */
    uint32_t      nr_ends_vmstate;

    /* TIMA mapping address */
    hwaddr        tm_base;
    MemoryRegion  tm_mmio;

    /* KVM support */
    int           fd;
    void          *tm_mmap;
    MemoryRegion  tm_mmio_kvm;
    VMChangeStateEntry *change;

    uint8_t       hv_prio;
} SpaprXive;

typedef struct SpaprXiveClass {
    XiveRouterClass parent;

    DeviceRealize parent_realize;
} SpaprXiveClass;

/*
 * The sPAPR machine has a unique XIVE IC device. Assign a fixed value
 * to the controller block id value. It can nevertheless be changed
 * for testing purpose.
 */
#define SPAPR_XIVE_BLOCK_ID 0x0

void spapr_xive_pic_print_info(SpaprXive *xive, Monitor *mon);

struct SpaprMachineState;
void spapr_xive_hcall_init(struct SpaprMachineState *spapr);
void spapr_xive_mmio_set_enabled(SpaprXive *xive, bool enable);
void spapr_xive_map_mmio(SpaprXive *xive);

int spapr_xive_end_to_target(uint8_t end_blk, uint32_t end_idx,
                             uint32_t *out_server, uint8_t *out_prio);
uint32_t spapr_xive_nr_ends(const SpaprXive *xive);

/*
 * KVM XIVE device helpers
 */
int kvmppc_xive_connect(SpaprInterruptController *intc, uint32_t nr_servers,
                        Error **errp);
void kvmppc_xive_disconnect(SpaprInterruptController *intc);
void kvmppc_xive_reset(SpaprXive *xive, Error **errp);
int kvmppc_xive_set_source_config(SpaprXive *xive, uint32_t lisn, XiveEAS *eas,
                                  Error **errp);
void kvmppc_xive_sync_source(SpaprXive *xive, uint32_t lisn, Error **errp);
uint64_t kvmppc_xive_esb_rw(XiveSource *xsrc, int srcno, uint32_t offset,
                            uint64_t data, bool write);
int kvmppc_xive_set_queue_config(SpaprXive *xive, uint8_t end_blk,
                                 uint32_t end_idx, XiveEND *end,
                                 Error **errp);
int kvmppc_xive_get_queue_config(SpaprXive *xive, uint8_t end_blk,
                                 uint32_t end_idx, XiveEND *end,
                                 Error **errp);
void kvmppc_xive_synchronize_state(SpaprXive *xive, Error **errp);
int kvmppc_xive_pre_save(SpaprXive *xive);
int kvmppc_xive_post_load(SpaprXive *xive, int version_id);

#endif /* PPC_SPAPR_XIVE_H */
