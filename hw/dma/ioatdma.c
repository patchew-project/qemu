/*
 * Intel(R) I/OAT DMA engine emulation
 *
 * Copyright (c) 2024 Nikita Shubin <nshubin@yadro.com>
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
#include "qemu/osdep.h"

#include "chardev/char-fe.h"
#include "chardev/char-socket.h"
#include "hw/hw.h"
#include "hw/pci/msi.h"
#include "hw/pci/msix.h"
#include "hw/pci/pci.h"
#include "hw/qdev-properties-system.h"
#include "hw/qdev-properties.h"
#include "migration/blocker.h"
#include "migration/vmstate.h"
#include "qapi/error.h"
#include "qapi/visitor.h"
#include "qemu/cutils.h"
#include "qemu/error-report.h"
#include "qemu/event_notifier.h"
#include "qemu/log.h"
#include "qemu/module.h"
#include "qemu/sockets.h"
#include "qemu/units.h"
#include "qom/object_interfaces.h"
#include "sysemu/hostmem.h"
#include "sysemu/kvm.h"
#include "sysemu/qtest.h"

#include "trace.h"

#define PCI_VENDOR_ID_INTEL                 0x8086
#define PCI_DEVICE_ID_INTEL_IOAT_SKX        0x2021

#define IOATDMA_BAR0_SIZE                   (16 * KiB)
#define IOATDMA_MSIX_SIZE                   (16 * KiB)
#define IOATDMA_MSIX_TABLE                  (0x2000)
#define IOATDMA_MSIX_PBA                    (0x3000)

/*  8-bit */
#define IOAT_CHANCNT_OFFSET                 0x00
/*  8-bit */
#define IOAT_XFERCAP_OFFSET                 0x01
/*  8-bit */
#define IOAT_XFERCAP_MASK                   0x1f

/*  8-bit, unused */
#define IOAT_GENCTRL_OFFSET                 0x02
#define IOAT_GENCTRL_DEBUG_EN               0x01

/*  8-bit */
#define IOAT_INTRCTRL_OFFSET                0x03
/* Master Interrupt Enable */
#define IOAT_INTRCTRL_MASTER_INT_EN         0x01
/* ATTNSTATUS -or- Channel Int */
#define IOAT_INTRCTRL_INT_STATUS            0x02
/* INT_STATUS -and- MASTER_INT_EN */
#define IOAT_INTRCTRL_INT                   0x04
/* Enable all MSI-X vectors */
#define IOAT_INTRCTRL_MSIX_VECTOR_CONTROL   0x08

/* Each bit is a channel */
#define IOAT_ATTNSTATUS_OFFSET              0x04

/*  8-bit */
#define IOAT_VER_OFFSET                     0x08

/* 16-bit */
#define IOAT_PERPORTOFFSET_OFFSET           0x0A

/* 16-bit */
#define IOAT_INTRDELAY_OFFSET               0x0C
/* Interrupt Delay Time */
#define IOAT_INTRDELAY_MASK                 0x3FFF
/* Interrupt Coalescing Supported */
#define IOAT_INTRDELAY_COALESE_SUPPORT      0x8000

/* 16-bit */
#define IOAT_DEVICE_STATUS_OFFSET           0x0E
#define IOAT_DEVICE_STATUS_DEGRADED_MODE    0x0001
#define IOAT_DEVICE_MMIO_RESTRICTED         0x0002
#define IOAT_DEVICE_MEMORY_BYPASS           0x0004
#define IOAT_DEVICE_ADDRESS_REMAPPING       0x0008

/* 32-bit */
#define IOAT_DMA_CAP_OFFSET                 0x10
#define IOAT_CAP_PAGE_BREAK                 0x00000001
#define IOAT_CAP_CRC                        0x00000002
#define IOAT_CAP_SKIP_MARKER                0x00000004
#define IOAT_CAP_DCA                        0x00000010
#define IOAT_CAP_CRC_MOVE                   0x00000020
#define IOAT_CAP_FILL_BLOCK                 0x00000040
#define IOAT_CAP_APIC                       0x00000080
#define IOAT_CAP_XOR                        0x00000100
#define IOAT_CAP_PQ                         0x00000200
#define IOAT_CAP_DWBES                      0x00002000
#define IOAT_CAP_RAID16SS                   0x00020000
#define IOAT_CAP_DPS                        0x00800000

/* CHWPREFLMT */
#define IOAT_PREFETCH_LIMIT_OFFSET          0x4C

#define IOATDMA_CHAN_CNT        4
#define IOATDMA_CHAN_STRIDE     0x80
#define IOATDMA_CHAN_OFFSET     IOATDMA_CHAN_STRIDE
#define IOATDMA_CHAN_END        ((IOATDMA_CHAN_CNT + 1) * IOATDMA_CHAN_STRIDE)

/* DMA Channel Registers */
/* 16-bit Channel Control Register */
#define IOAT_CHANCTRL_OFFSET                            0x00
#define IOAT_CHANCTRL_CHANNEL_PRIORITY_MASK             0xF000
#define IOAT3_CHANCTRL_COMPL_DCA_EN                     BIT(9)
#define IOAT_CHANCTRL_CHANNEL_IN_USE                    BIT(8)
#define IOAT_CHANCTRL_DESCRIPTOR_ADDR_SNOOP_CONTROL     BIT(5)
#define IOAT_CHANCTRL_ERR_INT_EN                        BIT(4)
#define IOAT_CHANCTRL_ANY_ERR_ABORT_EN                  BIT(3)
#define IOAT_CHANCTRL_ERR_COMPLETION_EN                 BIT(2)
#define IOAT_CHANCTRL_INT_REARM                         BIT(0)
#define IOAT_CHANCTRL_RUN \
    (IOAT_CHANCTRL_INT_REARM \
     | IOAT_CHANCTRL_ERR_INT_EN \
     | IOAT_CHANCTRL_ERR_COMPLETION_EN \
     | IOAT_CHANCTRL_ANY_ERR_ABORT_EN)

/* 16-bit DMA channel compatibility */
#define IOAT_DMA_COMP_OFFSET                0x02
/* Compatibility with DMA version 1 */
#define IOAT_DMA_COMP_V1                    0x0001
/* Compatibility with DMA version 2 */
#define IOAT_DMA_COMP_V2                    0x0002

/*  8-bit DMA Channel Command Register */
#define IOAT_CHANCMD_OFFSET                 0x04
#define IOAT_CHANCMD_RESET                  0x20
#define IOAT_CHANCMD_RESUME                 0x10
#define IOAT_CHANCMD_ABORT                  0x08
#define IOAT_CHANCMD_SUSPEND                0x04
#define IOAT_CHANCMD_APPEND                 0x02
#define IOAT_CHANCMD_START                  0x01

/* 16-bit DMA Count register */
#define IOAT_CHAN_DMACOUNT_OFFSET           0x06

/* 64-bit Channel Status Register */
#define IOAT_CHANSTS_OFFSET                     0x08
#define IOAT_CHANSTS_COMPLETED_DESCRIPTOR_ADDR  (~0x3fULL)
#define IOAT_CHANSTS_SOFT_ERR                   0x10ULL
#define IOAT_CHANSTS_UNAFFILIATED_ERR           0x8ULL
#define IOAT_CHANSTS_STATUS                     0x7ULL
#define IOAT_CHANSTS_ACTIVE                     0x0
#define IOAT_CHANSTS_DONE                       0x1
#define IOAT_CHANSTS_SUSPENDED                  0x2
#define IOAT_CHANSTS_HALTED                     0x3

#define IOAT_CHAINADDR_OFFSET_LOW           0x10
#define IOAT_CHAINADDR_OFFSET_HIGH          0x14

#define IOAT_CHANCMP_OFFSET_LOW             0x18
#define IOAT_CHANCMP_OFFSET_HIGH            0x1C

/* 32-bit Channel Error Register */
#define IOAT_CHANERR_OFFSET                 0x28
#define IOAT_CHANERR_SRC_ADDR_ERR           0x0001
#define IOAT_CHANERR_DEST_ADDR_ERR          0x0002
#define IOAT_CHANERR_NEXT_ADDR_ERR          0x0004
#define IOAT_CHANERR_NEXT_DESC_ALIGN_ERR    0x0008
#define IOAT_CHANERR_CHAIN_ADDR_VALUE_ERR   0x0010
#define IOAT_CHANERR_CHANCMD_ERR            0x0020
#define IOAT_CHANERR_CHIPSET_UNCORRECTABLE_DATA_INTEGRITY_ERR   0x0040
#define IOAT_CHANERR_DMA_UNCORRECTABLE_DATA_INTEGRITY_ERR       0x0080
#define IOAT_CHANERR_READ_DATA_ERR          0x0100
#define IOAT_CHANERR_WRITE_DATA_ERR         0x0200
#define IOAT_CHANERR_CONTROL_ERR            0x0400
#define IOAT_CHANERR_LENGTH_ERR             0x0800
#define IOAT_CHANERR_COMPLETION_ADDR_ERR    0x1000
#define IOAT_CHANERR_INT_CONFIGURATION_ERR  0x2000
#define IOAT_CHANERR_SOFT_ERR               0x4000
#define IOAT_CHANERR_UNAFFILIATED_ERR       0x8000
#define IOAT_CHANERR_XOR_P_OR_CRC_ERR       0x10000
#define IOAT_CHANERR_XOR_Q_ERR              0x20000
#define IOAT_CHANERR_DESCRIPTOR_COUNT_ERR   0x40000

/* 32 bit Direct Cache Access Control Register */
#define IOAT_DCACTRL_OFFSET                 0x30

#define IOAT_VER_3_4                        0x34    /* Version 3.4 */

#define IOAT_DESC_SZ                        64

typedef struct ioat_dma_descriptor {
    uint32_t size;
    union {
        uint32_t ctl;
        struct {
            unsigned int int_en:1;
            unsigned int src_snoop_dis:1;
            unsigned int dest_snoop_dis:1;
            unsigned int compl_write:1;
            unsigned int fence:1;
            unsigned int null:1;
            unsigned int src_brk:1;
            unsigned int dest_brk:1;
            unsigned int bundle:1;
            unsigned int dest_dca:1;
            unsigned int hint:1;
            unsigned int rsvd2:13;
            #define IOAT_OP_COPY 0x00
            unsigned int op:8;
        } ctl_f;
    };
    uint64_t    src_addr;
    uint64_t    dst_addr;
    uint64_t    next;
    uint64_t    rsv1;
    uint64_t    rsv2;
    /* store some driver data in an unused portion of the descriptor */
    union {
        uint64_t    user1;
        uint64_t    tx_cnt;
    };
    uint64_t    user2;
} ioat_dma_descriptor;

typedef struct IoatDmaDescriptorState {
    ioat_dma_descriptor desc;
    hwaddr addr;
    QSIMPLEQ_ENTRY(IoatDmaDescriptorState) desc_list;
} IoatDmaDescriptorState;

typedef struct IoatdmaState IoatdmaState;

typedef struct IoatdmaChanState {
    IoatdmaState *parent;

    uint8_t idx;

    uint16_t dma_count;

    uint32_t offset_low;
    uint32_t offset_high;

    uint32_t chancomp_low;
    uint32_t chancomp_high;

    uint16_t ctrl;
    uint64_t status;

    uint32_t chanerr;

    uint32_t dcactrl;

    QEMUBH *bh;
    uint16_t dma_idx;
    hwaddr next;
    QSIMPLEQ_HEAD(, IoatDmaDescriptorState) list;
} IoatdmaChanState;

typedef struct IoatdmaState {
    /*< private >*/
    PCIDevice parent_obj;

    /*< public >*/
    MemoryRegion mmio;         /* BAR 0 (registers) */
    MemoryRegion msix;         /* MSIX */

    uint8_t intrctrl;
    uint8_t intrdelay;
    uint8_t prefetch_limit;

    uint32_t dmacap;
    uint32_t attnsts;

    uint8_t chancnt;
    uint8_t xfercap_log;
    IoatdmaChanState chans[IOATDMA_CHAN_CNT];
} IoatdmaState;

#define TYPE_INTEL_IOATDMA "intel-ioatdma"
DECLARE_INSTANCE_CHECKER(IoatdmaState, INTEL_IOATDMA,
                         TYPE_INTEL_IOATDMA)

static const char *ioatdma_reg_to_str(hwaddr addr)
{
    switch (addr) {
    case IOAT_CHANCTRL_OFFSET:
        return "IOAT_CHANCTRL_OFFSET";
    case IOAT_DMA_COMP_OFFSET:
        return "IOAT_DMA_COMP_OFFSET";
    case IOAT_CHANCMD_OFFSET:
        return "IOAT_CHANCMD_OFFSET";
    case IOAT_CHAN_DMACOUNT_OFFSET:
        return "IOAT_CHAN_DMACOUNT_OFFSET";
    case IOAT_CHANSTS_OFFSET:
        return "IOAT_CHANSTS_OFFSET";
    case IOAT_CHAINADDR_OFFSET_LOW:
        return "IOAT_CHAINADDR_OFFSET_LOW";
    case IOAT_CHAINADDR_OFFSET_HIGH:
        return "IOAT_CHAINADDR_OFFSET_HIGH";
    case IOAT_CHANCMP_OFFSET_LOW:
        return "IOAT_CHANCMP_OFFSET_LOW";
    case IOAT_CHANCMP_OFFSET_HIGH:
        return "IOAT_CHANCMP_OFFSET_HIGH";
    case IOAT_CHANERR_OFFSET:
        return "IOAT_CHANERR_OFFSET";
    case IOAT_DCACTRL_OFFSET:
        return "IOAT_DCACTRL_OFFSET";
    default:
        return "(unknown)";
    };
}

static uint32_t ioatdma_chan_read(IoatdmaState *s, hwaddr addr, unsigned size)
{
    uint8_t idx = (addr - IOATDMA_CHAN_OFFSET) / IOATDMA_CHAN_STRIDE;
    hwaddr reg = (addr - IOATDMA_CHAN_OFFSET) % IOATDMA_CHAN_STRIDE;
    IoatdmaChanState *chan;
    uint32_t val32 = 0;

    if (idx > IOATDMA_CHAN_CNT) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: (%d) exceeds max supported channels (%d)\n",
                      __func__, idx, IOATDMA_CHAN_CNT);
        return 0;
    }

    chan = &s->chans[idx];

    switch (reg) {
    case IOAT_CHANCTRL_OFFSET:
        break;
    case IOAT_DMA_COMP_OFFSET:
        break;
    case IOAT_CHANCMD_OFFSET:
        val32 = chan->status;
        break;
    case IOAT_CHAINADDR_OFFSET_LOW:
        val32 = chan->offset_low;
        break;
    case IOAT_CHAINADDR_OFFSET_HIGH:
        val32 = chan->offset_high;
        break;
    case IOAT_CHANCMP_OFFSET_LOW:
        val32 = chan->chancomp_low;
        break;
    case IOAT_CHANCMP_OFFSET_HIGH:
        val32 = chan->chancomp_high;
        break;
    case IOAT_CHANSTS_OFFSET:
        val32 = chan->status;
        break;
    case IOAT_CHANERR_OFFSET:
        val32 = chan->chanerr;
        break;
    case IOAT_DCACTRL_OFFSET:
        val32 = chan->dcactrl;
        break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: read to unimplemented chan[%d] register " \
                      "at address: 0x%" PRIx64 " size %d\n",
                      __func__, idx, reg, size);
        break;
    }

    trace_ioatdma_chan_read(ioatdma_reg_to_str(reg), idx, addr,
                            reg, val32, size);

    return val32;
}

static MemTxResult ioatdma_io_read(void *opaque,
                                   hwaddr addr,
                                   uint64_t *data,
                                   unsigned size,
                                   MemTxAttrs attrs)
{
    IoatdmaState *s = INTEL_IOATDMA(opaque);
    uint32_t val32 = 0;

    switch (addr) {
    case IOAT_CHANCNT_OFFSET:
        val32 = IOATDMA_CHAN_CNT;
        break;
    case IOAT_XFERCAP_OFFSET:
        val32 = s->xfercap_log;
        break;
    case IOAT_INTRCTRL_OFFSET:
        val32 = s->intrctrl;
        break;
    case IOAT_ATTNSTATUS_OFFSET: /* 32-bit */
        val32 = s->attnsts;
        break;
    case IOAT_VER_OFFSET:
        val32 = IOAT_VER_3_4;
        break;
    case IOAT_INTRDELAY_OFFSET:
        val32 = s->intrdelay;
        break;
    case IOAT_DMA_CAP_OFFSET:
        val32 = s->dmacap;
        break;
    case IOAT_PREFETCH_LIMIT_OFFSET:
        val32 = s->prefetch_limit;
        break;
    case IOATDMA_CHAN_OFFSET ... IOATDMA_CHAN_END:
        val32 = ioatdma_chan_read(s, addr, size);
        *data = val32;
        return MEMTX_OK;
    case IOAT_GENCTRL_OFFSET: /* unused*/
    case IOAT_PERPORTOFFSET_OFFSET: /* unused*/
    case IOAT_DEVICE_STATUS_OFFSET: /* unused*/
    default: /* unimplemented */
            qemu_log_mask(LOG_GUEST_ERROR,
                          "%s:  read to unimplemented register " \
                          "at address: 0x%" PRIx64 " size %d\n",
                          __func__, addr, size);
        break;
    }

    trace_ioatdma_io_read(addr, val32, size);
    *data = val32;

    return MEMTX_OK;
}

static void ioatdma_reset_chainaddr(IoatdmaChanState *chan)
{
    chan->next = chan->offset_low | ((hwaddr)chan->offset_high << 32);
    chan->dma_idx = 0;
}

static void ioatdma_set_completion(IoatdmaChanState *chan, uint64_t status)
{
    PCIDevice *dev = PCI_DEVICE(chan->parent);
    dma_addr_t compaddr;

    compaddr = chan->chancomp_low;
    compaddr |= (dma_addr_t)chan->chancomp_high << 32;
    pci_dma_write(dev, compaddr, &status, 8);
    trace_ioatdma_write_completion(chan->idx, compaddr, status);
}

static hwaddr ioatdma_transfer_one(IoatdmaChanState *chan, hwaddr chanaddr,
                                   uint8_t *data, uint32_t transfer_size,
                                   ioat_dma_descriptor *desc)
{
    PCIDevice *dev = PCI_DEVICE(chan->parent);
    MemTxResult result = MEMTX_ERROR;
    uint64_t n = 0, tail;

    trace_ioatdma_transfer_one(chanaddr, desc->next, desc->size,
                               desc->src_addr, desc->dst_addr, desc->ctl,
                               desc->ctl_f.int_en, desc->ctl_f.compl_write);

    if (desc->ctl_f.null) {
        return MEMTX_OK;
    }

    /* check if src addr is valid */
    if (!address_space_access_valid(pci_get_address_space(dev), desc->src_addr,
                                    desc->size, true, MEMTXATTRS_UNSPECIFIED)) {
        chan->status = IOAT_CHANSTS_SOFT_ERR;
        chan->chanerr = IOAT_CHANERR_SRC_ADDR_ERR;
        goto err;
    }

    /* check if dst addr is valid */
    if (!address_space_access_valid(pci_get_address_space(dev), desc->dst_addr,
                                    desc->size, true, MEMTXATTRS_UNSPECIFIED)) {
        chan->status = IOAT_CHANSTS_SOFT_ERR;
        chan->chanerr = IOAT_CHANERR_DEST_ADDR_ERR;
        goto err;
    }

    tail = desc->size % transfer_size;

    while (n < (desc->size - tail)) {
        pci_dma_read(dev, (dma_addr_t)desc->src_addr,
                     data, (dma_addr_t)transfer_size);
        pci_dma_write(dev, (dma_addr_t)desc->dst_addr,
                      data, (dma_addr_t)transfer_size);
        desc->src_addr += transfer_size;
        desc->dst_addr += transfer_size;

        n += transfer_size;
    }

    if (tail) {
        pci_dma_read(dev, (dma_addr_t)desc->src_addr, data, (dma_addr_t)tail);
        pci_dma_write(dev, (dma_addr_t)desc->dst_addr, data, (dma_addr_t)tail);
    }

    result = MEMTX_OK;

err:
    return result;
}

static MemTxResult ioatdma_transfer_batch(IoatdmaState *s,
                                          IoatdmaChanState *chan)
{
    PCIDevice *dev = PCI_DEVICE(s);
    MemTxResult result = MEMTX_OK;
    uint16_t dma_count = chan->dma_count;
    uint16_t idx = chan->dma_idx;
    ioat_dma_descriptor desc;
    hwaddr next = chan->next;
    uint32_t transfer_size = (1 << s->xfercap_log);
    uint64_t status;
    uint8_t *data = g_malloc(transfer_size);

    trace_ioatdma_transfer_batch(next, transfer_size, idx, dma_count);

    chan->status = IOAT_CHANSTS_ACTIVE;
    while (idx != dma_count) {
        result = pci_dma_read(dev, next, &desc, IOAT_DESC_SZ);
        if (result != MEMTX_OK) {
            chan->status = IOAT_CHANSTS_SOFT_ERR;
            chan->chanerr = IOAT_CHANERR_CHAIN_ADDR_VALUE_ERR;
            goto err;
        }

        result = ioatdma_transfer_one(chan, next, data, transfer_size, &desc);
        if (result != MEMTX_OK) {
            goto err;
        }

        if (desc.ctl_f.compl_write) {
            status = next & ~0x3fULL;
            ioatdma_set_completion(chan, status);
        }

        if (desc.ctl_f.int_en) {
            msix_notify(dev, chan->idx);
        }

        next = desc.next;
        idx++;
    }

    if (result == MEMTX_OK) {
        chan->status = IOAT_CHANSTS_DONE;
    }

err:
    g_free(data);
    chan->next = next;
    chan->dma_idx = idx;
    return result;
}

static void ioatdma_transfer_bh(void *opaque)
{
    IoatdmaChanState *chan = opaque;
    IoatdmaState *s = chan->parent;
    PCIDevice *dev = PCI_DEVICE(s);
    MemTxResult result = ioatdma_transfer_batch(s, chan);

    /* check if any errors */
    if (result != MEMTX_OK) {
        if (chan->ctrl & IOAT_CHANCTRL_ERR_INT_EN) {
            msix_notify(dev, chan->idx);
        }
    }
}

static void ioatdma_chan_write(IoatdmaState *s, hwaddr addr,
                               uint64_t data, unsigned size)
{
    uint8_t idx = (addr - IOATDMA_CHAN_OFFSET) / IOATDMA_CHAN_STRIDE;
    hwaddr reg = (addr - IOATDMA_CHAN_OFFSET) % IOATDMA_CHAN_STRIDE;
    IoatdmaChanState *chan;

    if (idx > IOATDMA_CHAN_CNT) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: (%d) exceeds max supported channels (%d)\n",
                      __func__, idx, IOATDMA_CHAN_CNT);
        return;
    }

    chan = &s->chans[idx];
    trace_ioatdma_chan_write(ioatdma_reg_to_str(reg), idx, addr,
                             reg, data, size);

    switch (reg) {
    case IOAT_CHANCTRL_OFFSET:
        if (chan->dma_count && (data & IOAT_CHANCTRL_RUN)) {
            /* qemu_bh_schedule(chan->bh); */
        }
        chan->ctrl = data;
        break;
    case IOAT_DMA_COMP_OFFSET:
        break;
    case IOAT_CHANCMD_OFFSET:
        if (data & IOAT_CHANCMD_SUSPEND) {
            chan->status = IOAT_CHANSTS_SUSPENDED;
        }

        if (data & IOAT_CHANCMD_RESET) {
            chan->dma_count = 0;
        }
        break;
    case IOAT_CHAN_DMACOUNT_OFFSET:
        chan->dma_count = data;
        qemu_bh_schedule(chan->bh);
        break;
    case IOAT_CHAINADDR_OFFSET_LOW:
        chan->offset_low = data;
        ioatdma_reset_chainaddr(chan);
        break;
    case IOAT_CHAINADDR_OFFSET_HIGH:
        chan->offset_high = data;
        ioatdma_reset_chainaddr(chan);
        break;
    case IOAT_CHANCMP_OFFSET_LOW:
        chan->chancomp_low = data;
        break;
    case IOAT_CHANCMP_OFFSET_HIGH:
        chan->chancomp_high = data;
        break;
    case IOAT_CHANSTS_OFFSET:
        chan->status = data;
        break;
    case IOAT_CHANERR_OFFSET:
        chan->chanerr = data;
        /* TODO: clear interrupts ? */
        break;
    case IOAT_DCACTRL_OFFSET:
        chan->dcactrl = data;
        break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: write to unimplemented chan[%d] register " \
                      "at address: 0x%" PRIx64 " val 0x%" PRIx64 " size %d\n",
                      __func__, idx, reg, data, size);
        break;
    }
}

static MemTxResult ioatdma_io_write(void *opaque,
                                    hwaddr addr,
                                    uint64_t data,
                                    unsigned size,
                                    MemTxAttrs attrs)
{
    IoatdmaState *s = INTEL_IOATDMA(opaque);

    switch (addr) {
    case IOAT_INTRCTRL_OFFSET:
        s->intrctrl = data;
        break;
    case IOAT_ATTNSTATUS_OFFSET: /* 32-bit */
        s->attnsts = data;
        break;
    case IOAT_INTRDELAY_OFFSET:
        s->intrdelay = data;
        break;
    case IOAT_PREFETCH_LIMIT_OFFSET:
        s->prefetch_limit = data;
        break;
    case IOATDMA_CHAN_OFFSET ... IOATDMA_CHAN_END:
        ioatdma_chan_write(s, addr, data, size);
        return MEMTX_OK;
    case IOAT_DMA_CAP_OFFSET:
    case IOAT_CHANCNT_OFFSET:
    case IOAT_XFERCAP_OFFSET:
    case IOAT_VER_OFFSET:
    case IOAT_GENCTRL_OFFSET: /* unused*/
    case IOAT_PERPORTOFFSET_OFFSET: /* unused*/
    case IOAT_DEVICE_STATUS_OFFSET: /* unused*/
    default: /* unimplemented */
            qemu_log_mask(LOG_GUEST_ERROR,
                          "%s:  read to unimplemented register " \
                          "at address: 0x%" PRIx64 " size %d\n",
                          __func__, addr, size);
        break;
    }

    trace_ioatdma_io_write(addr, data, size);

    return MEMTX_OK;
}

static const MemoryRegionOps ioatdma_mmio_ops = {
    .read_with_attrs = ioatdma_io_read,
    .write_with_attrs = ioatdma_io_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
    .valid = {
        .min_access_size = 1,
        .max_access_size = 8,
    },
    .impl = {
        .min_access_size = 1,
        .max_access_size = 8,
    },
};

static void ioatdma_realize(PCIDevice *dev, Error **errp)
{
    IoatdmaState *s = INTEL_IOATDMA(dev);
    IoatdmaChanState *chan;
    uint8_t *pci_conf;
    int res, i;

    if (s->chancnt > IOATDMA_CHAN_CNT) {
        error_setg(errp, "maximum %d chancnt supported", IOATDMA_CHAN_CNT);
        return;
    }

    if (s->xfercap_log & ~IOAT_XFERCAP_MASK) {
        error_setg(errp, "maximum %d xfercap_log supported", IOAT_XFERCAP_MASK);
        return;
    }

    pci_conf = dev->config;
    pci_conf[PCI_COMMAND] = PCI_COMMAND_IO | PCI_COMMAND_MEMORY;

    memory_region_init_io(&s->mmio, OBJECT(s), &ioatdma_mmio_ops, s,
                          "intel-ioatdma.mmio", IOATDMA_BAR0_SIZE);

    pci_register_bar(dev, 0, PCI_BASE_ADDRESS_SPACE_MEMORY |
                     PCI_BASE_ADDRESS_MEM_TYPE_64,
                     &s->mmio);

    res = msix_init(PCI_DEVICE(s), IOATDMA_CHAN_CNT,
                    &s->mmio,
                    0, IOATDMA_MSIX_TABLE,
                    &s->mmio,
                    0, IOATDMA_MSIX_PBA,
                    0xA0, NULL);

    if (res) {
        error_setg(errp, "msix_init failed!");
        return;
    }

    for (i = 0; i < IOATDMA_CHAN_CNT; i++) {
        chan = &s->chans[i];
        chan->parent = s;
        chan->idx = i;
        QSIMPLEQ_INIT(&chan->list);

        /* TODO: guarded bh */
        chan->bh = qemu_bh_new(ioatdma_transfer_bh, chan);
        msix_vector_use(dev, i);
    }
}

static uint32_t ioatdma_config_read(PCIDevice *dev,
                                    uint32_t addr, int len)
{
    uint32_t val32;

    val32 = pci_default_read_config(dev, addr, len);
    trace_ioatdma_config_read(addr, val32, len);

    return val32;
}

static void ioatdma_config_write(PCIDevice *dev, uint32_t addr,
                                 uint32_t val, int len)
{
    pci_default_write_config(dev, addr, val, len);
    trace_ioatdma_config_write(addr, val, len);
}

static void ioatdma_reset(DeviceState *dev)
{
    IoatdmaState *s = INTEL_IOATDMA(dev);
    int i;

    for (i = 0; i < IOATDMA_CHAN_CNT; i++) {
        s->chans[i].dma_count = 0;
        s->chans[i].dma_idx = 0;
        s->chans[i].offset_low = 0;
        s->chans[i].offset_high = 0;
        s->chans[i].chancomp_low = 0;
        s->chans[i].chancomp_high = 0;
        s->chans[i].status = IOAT_CHANSTS_DONE;
    }
}

static Property ioatdma_properties[] = {
    DEFINE_PROP_UINT8("chan_cnt", IoatdmaState, chancnt, IOATDMA_CHAN_CNT),
    DEFINE_PROP_UINT8("xfercap_log", IoatdmaState, xfercap_log, 21),
    DEFINE_PROP_END_OF_LIST(),
};

static void ioatdma_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    PCIDeviceClass *k = PCI_DEVICE_CLASS(klass);

    k->realize = ioatdma_realize;
    k->config_read = ioatdma_config_read;
    k->config_write = ioatdma_config_write;
    dc->reset = ioatdma_reset;

    k->vendor_id = PCI_VENDOR_ID_INTEL;
    k->device_id = PCI_DEVICE_ID_INTEL_IOAT_SKX;
    k->class_id = PCI_CLASS_SYSTEM_DMA;

    set_bit(DEVICE_CATEGORY_MISC, dc->categories);
    dc->desc = "Intel(R) I/OAT DMA engine";

    device_class_set_props(dc, ioatdma_properties);
}

static const TypeInfo ioatdma_info = {
    .name          = TYPE_INTEL_IOATDMA,
    .parent        = TYPE_PCI_DEVICE,
    .instance_size = sizeof(IoatdmaState),
    .class_init    = ioatdma_class_init,
    .interfaces = (InterfaceInfo[]) {
        { INTERFACE_PCIE_DEVICE },
        { },
    },
};

static void ioatdma_register_types(void)
{
    type_register_static(&ioatdma_info);
}

type_init(ioatdma_register_types)
