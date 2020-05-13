/*
 * QEMU PowerPC XIVE2 interrupt controller model  (POWER10)
 *
 * Copyright (c) 2019-2020, IBM Corporation.
 *
 * This code is licensed under the GPL version 2 or later. See the
 * COPYING file in the top-level directory.
 *
 */

#ifndef PPC_XIVE2_H
#define PPC_XIVE2_H

#include "hw/ppc/xive2_regs.h"

/*
 * XIVE2 Router (POWER10)
 */
typedef struct Xive2Router {
    SysBusDevice    parent;

    XiveFabric *xfb;
} Xive2Router;

#define TYPE_XIVE2_ROUTER TYPE_XIVE_ROUTER "2"
#define XIVE2_ROUTER(obj)                                \
    OBJECT_CHECK(Xive2Router, (obj), TYPE_XIVE2_ROUTER)
#define XIVE2_ROUTER_CLASS(klass)                                        \
    OBJECT_CLASS_CHECK(Xive2RouterClass, (klass), TYPE_XIVE2_ROUTER)
#define XIVE2_ROUTER_GET_CLASS(obj)                              \
    OBJECT_GET_CLASS(Xive2RouterClass, (obj), TYPE_XIVE2_ROUTER)

typedef struct Xive2RouterClass {
    SysBusDeviceClass parent;

    /* XIVE table accessors */
    int (*get_eas)(Xive2Router *xrtr, uint8_t eas_blk, uint32_t eas_idx,
                   Xive2Eas *eas);
    int (*get_end)(Xive2Router *xrtr, uint8_t end_blk, uint32_t end_idx,
                   Xive2End *end);
    int (*write_end)(Xive2Router *xrtr, uint8_t end_blk, uint32_t end_idx,
                     Xive2End *end, uint8_t word_number);
    int (*get_nvp)(Xive2Router *xrtr, uint8_t nvt_blk, uint32_t nvt_idx,
                   Xive2Nvp *nvt);
    int (*write_nvp)(Xive2Router *xrtr, uint8_t nvt_blk, uint32_t nvt_idx,
                     Xive2Nvp *nvt, uint8_t word_number);
    uint8_t (*get_block_id)(Xive2Router *xrtr);
} Xive2RouterClass;

int xive2_router_get_eas(Xive2Router *xrtr, uint8_t eas_blk, uint32_t eas_idx,
                        Xive2Eas *eas);
int xive2_router_get_end(Xive2Router *xrtr, uint8_t end_blk, uint32_t end_idx,
                        Xive2End *end);
int xive2_router_write_end(Xive2Router *xrtr, uint8_t end_blk, uint32_t end_idx,
                          Xive2End *end, uint8_t word_number);
int xive2_router_get_nvp(Xive2Router *xrtr, uint8_t nvt_blk, uint32_t nvt_idx,
                        Xive2Nvp *nvt);
int xive2_router_write_nvp(Xive2Router *xrtr, uint8_t nvt_blk, uint32_t nvt_idx,
                          Xive2Nvp *nvt, uint8_t word_number);

void xive2_router_notify(XiveNotifier *xn, uint32_t lisn);

/*
 * XIVE2 END ESBs  (POWER10)
 */

#define TYPE_XIVE2_END_SOURCE TYPE_XIVE_END_SOURCE "2"
#define XIVE2_END_SOURCE(obj) \
    OBJECT_CHECK(Xive2EndSource, (obj), TYPE_XIVE2_END_SOURCE)

typedef struct Xive2EndSource {
    DeviceState parent;

    uint32_t        nr_ends;

    /* ESB memory region */
    uint32_t        esb_shift;
    MemoryRegion    esb_mmio;

    Xive2Router     *xrtr;
} Xive2EndSource;


#endif /* PPC_XIVE2_H */
