/*
 * QEMU INTEL 82576EB GbE NIC emulation
 *
 * Software developer's manuals:
 * https://www.intel.com/content/dam/www/public/us/en/documents/datasheets/82576eg-gbe-datasheet.pdf
 *
 * Authors:
 * Sriram Yagnaraman <sriram.yagnaraman@est.tech>
 *
 * Based on work done by:
 * Knut Omang.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, see <http://www.gnu.org/licenses/>.
 */

#ifndef HW_NET_IGB_H
#define HW_NET_IGB_H

#include "qemu/osdep.h"
#include "qemu/range.h"
#include "sysemu/sysemu.h"
#include "net/net.h"
#include "linux/virtio_net.h"
#include "hw/pci/pci.h"
#include "hw/pci/pcie.h"
#include "hw/pci/pcie_sriov.h"
#include "hw/hw.h"
#include "hw/pci/msi.h"
#include "hw/pci/msix.h"
#include "hw/qdev-properties.h"
#include "migration/vmstate.h"
#include "e1000x_common.h"
#include "igb_core.h"

#include "trace.h"
#include "qapi/error.h"


#define IGB_MMIO_IDX     0
#define IGB_FLASH_IDX    1
#define IGB_IO_IDX       2
#define IGB_MSIX_IDX     3

#define IGB_MMIO_SIZE    (128 * KiB)
#define IGB_FLASH_SIZE   (128 * KiB)
#define IGB_IO_SIZE      (32)
#define IGB_MSIX_SIZE    (16 * KiB)

#define IGBVF_MMIO_SIZE  (16 * KiB)
#define IGBVF_MSIX_SIZE  (16 * KiB)

#define IGB_MSIX_TABLE   (0x0000)
#define IGB_MSIX_PBA     (0x2000)

/* PCIe configuration space :
 * and in 6.10 Software accessed words. */
#define IGB_PCIE_PM_CAP_OFFSET    0x40
#define IGB_PCIE_MSI_CAP_OFFSET   0x50
#define IGB_PCIE_MSIX_CAP_OFFSET  0x70
#define IGB_PCIE_PCIE_CAP_OFFSET  0xA0
#define IGB_PCIE_AER_CAP_OFFSET   0x100
#define IGB_PCIE_SER_CAP_OFFSET   0x140
#define IGB_PCIE_ARI_CAP_OFFSET   0x150
#define IGB_PCIE_SRIOV_CAP_OFFSET 0x160

/* Supported Rx Buffer Sizes */
#define IGB_RXBUFFER_256    256
#define IGB_RXBUFFER_1536    1536
#define IGB_RXBUFFER_2048    2048
#define IGB_RXBUFFER_3072    3072
#define IGB_RX_HDR_LEN        IGB_RXBUFFER_256
#define IGB_TS_HDR_LEN        16

typedef struct IgbState IgbState;
typedef struct IgbvfState IgbvfState;

#define TYPE_IGB "igb"
#define IGB(obj)   OBJECT_CHECK(IgbState, (obj), TYPE_IGB)

#define TYPE_IGBVF "igbvf"
#define IGBVF(obj) OBJECT_CHECK(IgbvfState, (obj), TYPE_IGBVF)

struct IgbState {
    PCIDevice parent_obj;
    NICState *nic;
    NICConf conf;

    MemoryRegion mmio;
    MemoryRegion flash;
    MemoryRegion io;
    MemoryRegion msix;

    uint32_t ioaddr;

    uint16_t subsys_ven;
    uint16_t subsys;

    uint16_t subsys_ven_used;
    uint16_t subsys_used;

    IGBCore core;
} ;

struct IgbvfState {
    PCIDevice parent_obj;

    MemoryRegion mmio;
    MemoryRegion msix;

    IGBCore core;
};

static const VMStateDescription igb_vmstate_tx_ctx = {
    .name = "igb-tx-ctx",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_UINT16(ip_len, struct igb_tx_ctx),
        VMSTATE_UINT8(mac_len, struct igb_tx_ctx),
        VMSTATE_UINT16(vlan, struct igb_tx_ctx),
        VMSTATE_UINT16(tucmd, struct igb_tx_ctx),
        VMSTATE_UINT8(l4_len, struct igb_tx_ctx),
        VMSTATE_UINT16(mss, struct igb_tx_ctx),
        VMSTATE_UINT8(idx, struct igb_tx_ctx),
        VMSTATE_BOOL(valid, struct igb_tx_ctx),
        VMSTATE_END_OF_LIST()
    }
};

static const VMStateDescription igb_vmstate_tx = {
    .name = "igb-tx",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_BOOL(is_first, struct igb_tx),
        VMSTATE_UINT8(ctx_id, struct igb_tx),
        VMSTATE_BOOL(vlan_needed, struct igb_tx),
        VMSTATE_UINT8(sum_needed, struct igb_tx),
        VMSTATE_BOOL(cptse, struct igb_tx),
        VMSTATE_BOOL(skip_current_pkt, struct igb_tx),
        VMSTATE_END_OF_LIST()
    }
};

static const VMStateDescription igb_vmstate_intr_timer = {
    .name = "igb-intr-timer",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_TIMER_PTR(timer, IgbIntrDelayTimer),
        VMSTATE_BOOL(running, IgbIntrDelayTimer),
        VMSTATE_END_OF_LIST()
    }
};

#define VMSTATE_IGB_INTR_DELAY_TIMER(_f, _s)                     \
    VMSTATE_STRUCT(_f, _s, 0,                                       \
                   igb_vmstate_intr_timer, IgbIntrDelayTimer)

#define VMSTATE_IGB_INTR_DELAY_TIMER_ARRAY(_f, _s, _num)         \
    VMSTATE_STRUCT_ARRAY(_f, _s, _num, 0,                           \
                         igb_vmstate_intr_timer, IgbIntrDelayTimer)

#endif
