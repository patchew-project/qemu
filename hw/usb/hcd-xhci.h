/*
 * USB xHCI controller emulation
 *
 * Copyright (c) 2011 Securiforest
 * Date: 2011-05-11 ;  Author: Hector Martin <hector@marcansoft.com>
 * Based on usb-ohci.c, emulates Renesas NEC USB 3.0
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

#ifndef HW_USB_HCD_XHCI_H
#define HW_USB_HCD_XHCI_H
#include "qom/object.h"

#include "hw/usb.h"
#include "hw/usb/xhci.h"
#include "system/dma.h"

OBJECT_DECLARE_SIMPLE_TYPE(XHCIState, XHCI)

/* Very pessimistic, let's hope it's enough for all cases */
#define EV_QUEUE (((3 * 24) + 16) * XHCI_MAXSLOTS)

typedef struct XHCIStreamContext XHCIStreamContext;
typedef struct XHCIEPContext XHCIEPContext;

enum xhci_flags {
    XHCI_FLAG_ENABLE_STREAMS = 1,
};

typedef enum TRBType {
    TRB_RESERVED = 0,
    TR_NORMAL,
    TR_SETUP,
    TR_DATA,
    TR_STATUS,
    TR_ISOCH,
    TR_LINK,
    TR_EVDATA,
    TR_NOOP,
    CR_ENABLE_SLOT,
    CR_DISABLE_SLOT,
    CR_ADDRESS_DEVICE,
    CR_CONFIGURE_ENDPOINT,
    CR_EVALUATE_CONTEXT,
    CR_RESET_ENDPOINT,
    CR_STOP_ENDPOINT,
    CR_SET_TR_DEQUEUE,
    CR_RESET_DEVICE,
    CR_FORCE_EVENT,
    CR_NEGOTIATE_BW,
    CR_SET_LATENCY_TOLERANCE,
    CR_GET_PORT_BANDWIDTH,
    CR_FORCE_HEADER,
    CR_NOOP,
    ER_TRANSFER = 32,
    ER_COMMAND_COMPLETE,
    ER_PORT_STATUS_CHANGE,
    ER_BANDWIDTH_REQUEST,
    ER_DOORBELL,
    ER_HOST_CONTROLLER,
    ER_DEVICE_NOTIFICATION,
    ER_MFINDEX_WRAP,
    /* vendor specific bits */
    CR_VENDOR_NEC_FIRMWARE_REVISION  = 49,
    CR_VENDOR_NEC_CHALLENGE_RESPONSE = 50,
} TRBType;

typedef enum TRBCCode {
    CC_INVALID = 0,
    CC_SUCCESS,
    CC_DATA_BUFFER_ERROR,
    CC_BABBLE_DETECTED,
    CC_USB_TRANSACTION_ERROR,
    CC_TRB_ERROR,
    CC_STALL_ERROR,
    CC_RESOURCE_ERROR,
    CC_BANDWIDTH_ERROR,
    CC_NO_SLOTS_ERROR,
    CC_INVALID_STREAM_TYPE_ERROR,
    CC_SLOT_NOT_ENABLED_ERROR,
    CC_EP_NOT_ENABLED_ERROR,
    CC_SHORT_PACKET,
    CC_RING_UNDERRUN,
    CC_RING_OVERRUN,
    CC_VF_ER_FULL,
    CC_PARAMETER_ERROR,
    CC_BANDWIDTH_OVERRUN,
    CC_CONTEXT_STATE_ERROR,
    CC_NO_PING_RESPONSE_ERROR,
    CC_EVENT_RING_FULL_ERROR,
    CC_INCOMPATIBLE_DEVICE_ERROR,
    CC_MISSED_SERVICE_ERROR,
    CC_COMMAND_RING_STOPPED,
    CC_COMMAND_ABORTED,
    CC_STOPPED,
    CC_STOPPED_LENGTH_INVALID,
    CC_MAX_EXIT_LATENCY_TOO_LARGE_ERROR = 29,
    CC_ISOCH_BUFFER_OVERRUN = 31,
    CC_EVENT_LOST_ERROR,
    CC_UNDEFINED_ERROR,
    CC_INVALID_STREAM_ID_ERROR,
    CC_SECONDARY_BANDWIDTH_ERROR,
    CC_SPLIT_TRANSACTION_ERROR
} TRBCCode;

/* Register regions */
#define XHCI_REGS_LENGTH_CAP         0x40
#define XHCI_REGS_LENGTH_OPER        0x400
#define XHCI_REGS_LENGTH_PORT        (XHCI_PORT_PR_SZ * XHCI_MAXPORTS)
#define XHCI_REGS_LENGTH_RUNTIME     ((XHCI_MAXINTRS + 1) * XHCI_INTR_IR_SZ)
/* XXX: Should doorbell length be *4 rather than *32? */
#define XHCI_REGS_LENGTH_DOORBELL    ((XHCI_MAXSLOTS + 1) * 0x20)

#define XHCI_REGS_OFFSET_CAP         0
#define XHCI_REGS_OFFSET_OPER        (XHCI_REGS_OFFSET_CAP +   \
                                      XHCI_REGS_LENGTH_CAP)
#define XHCI_REGS_OFFSET_PORT        (XHCI_REGS_OFFSET_OPER +  \
                                      XHCI_REGS_LENGTH_OPER)
#define XHCI_REGS_OFFSET_RUNTIME     0x1000
#define XHCI_REGS_OFFSET_DOORBELL    0x2000

/* Register definitions */
#define XHCI_HCCAP_REG_CAPLENGTH            0x00
#define XHCI_HCCAP_REG_HCIVERSION           0x02
#define XHCI_HCCAP_REG_HCSPARAMS1           0x04
#define XHCI_HCCAP_REG_HCSPARAMS2           0x08
#define XHCI_HCCAP_REG_HCSPARAMS3           0x0C
#define XHCI_HCCAP_REG_HCCPARAMS1           0x10
#define   XHCI_HCCPARAMS1_AC64              0x00000001
#define   XHCI_HCCPARAMS1_XECP_SHIFT        16
#define   XHCI_HCCPARAMS1_MAXPSASIZE_SHIFT  12
#define XHCI_HCCAP_REG_DBOFF                0x14
#define XHCI_HCCAP_REG_RTSOFF               0x18
#define XHCI_HCCAP_REG_HCCPARAMS2           0x1C
#define XHCI_HCCAP_EXTCAP_START             0x20 /* SW-defined */

#define XHCI_PORT_PR_SZ                     0x10
#define XHCI_PORT_REG_PORTSC                0x00
#define   XHCI_PORTSC_CCS                   (1 << 0)
#define   XHCI_PORTSC_PED                   (1 << 1)
#define   XHCI_PORTSC_OCA                   (1 << 3)
#define   XHCI_PORTSC_PR                    (1 << 4)
#define   XHCI_PORTSC_PLS_SHIFT             5
#define   XHCI_PORTSC_PLS_MASK              0xf
#define   XHCI_PORTSC_PP                    (1 << 9)
#define   XHCI_PORTSC_SPEED_SHIFT           10
#define   XHCI_PORTSC_SPEED_MASK            0xf
#define   XHCI_PORTSC_SPEED_FULL            (1 << 10)
#define   XHCI_PORTSC_SPEED_LOW             (2 << 10)
#define   XHCI_PORTSC_SPEED_HIGH            (3 << 10)
#define   XHCI_PORTSC_SPEED_SUPER           (4 << 10)
#define   XHCI_PORTSC_PIC_SHIFT             14
#define   XHCI_PORTSC_PIC_MASK              0x3
#define   XHCI_PORTSC_LWS                   (1 << 16)
#define   XHCI_PORTSC_CSC                   (1 << 17)
#define   XHCI_PORTSC_PEC                   (1 << 18)
#define   XHCI_PORTSC_WRC                   (1 << 19)
#define   XHCI_PORTSC_OCC                   (1 << 20)
#define   XHCI_PORTSC_PRC                   (1 << 21)
#define   XHCI_PORTSC_PLC                   (1 << 22)
#define   XHCI_PORTSC_CEC                   (1 << 23)
#define   XHCI_PORTSC_CAS                   (1 << 24)
#define   XHCI_PORTSC_WCE                   (1 << 25)
#define   XHCI_PORTSC_WDE                   (1 << 26)
#define   XHCI_PORTSC_WOE                   (1 << 27)
#define   XHCI_PORTSC_DR                    (1 << 30)
#define   XHCI_PORTSC_WPR                   (1 << 31)
/* read/write bits */
#define   XHCI_PORTSC_RW_MASK               (XHCI_PORTSC_PP |    \
                                             XHCI_PORTSC_WCE |   \
                                             XHCI_PORTSC_WDE |   \
                                             XHCI_PORTSC_WOE)
/* write-1-to-clear bits*/
#define   XHCI_PORTSC_W1C_MASK              (XHCI_PORTSC_CSC |   \
                                             XHCI_PORTSC_PEC |   \
                                             XHCI_PORTSC_WRC |   \
                                             XHCI_PORTSC_OCC |   \
                                             XHCI_PORTSC_PRC |   \
                                             XHCI_PORTSC_PLC |   \
                                             XHCI_PORTSC_CEC)
#define XHCI_PORT_REG_PORTPMSC              0x04
#define XHCI_PORT_REG_PORTLI                0x08
#define XHCI_PORT_REG_PORTHLPMC             0x0C

#define XHCI_OPER_REG_USBCMD                0x00
#define   XHCI_USBCMD_RS                    (1 << 0)
#define   XHCI_USBCMD_HCRST                 (1 << 1)
#define   XHCI_USBCMD_INTE                  (1 << 2)
#define   XHCI_USBCMD_HSEE                  (1 << 3)
#define   XHCI_USBCMD_LHCRST                (1 << 7)
#define   XHCI_USBCMD_CSS                   (1 << 8)
#define   XHCI_USBCMD_CRS                   (1 << 9)
#define   XHCI_USBCMD_EWE                   (1 << 10)
#define   XHCI_USBCMD_EU3S                  (1 << 11)
#define XHCI_OPER_REG_USBSTS                0x04
#define   XHCI_USBSTS_HCH                   (1 << 0)
#define   XHCI_USBSTS_HSE                   (1 << 2)
#define   XHCI_USBSTS_EINT                  (1 << 3)
#define   XHCI_USBSTS_PCD                   (1 << 4)
#define   XHCI_USBSTS_SSS                   (1 << 8)
#define   XHCI_USBSTS_RSS                   (1 << 9)
#define   XHCI_USBSTS_SRE                   (1 << 10)
#define   XHCI_USBSTS_CNR                   (1 << 11)
#define   XHCI_USBSTS_HCE                   (1 << 12)
/* these bits are write-1-to-clear */
#define   XHCI_USBSTS_W1C_MASK              (XHCI_USBSTS_HSE |    \
                                             XHCI_USBSTS_EINT |   \
                                             XHCI_USBSTS_PCD |    \
                                             XHCI_USBSTS_SRE)
#define XHCI_OPER_REG_PAGESIZE              0x08
#define XHCI_OPER_REG_DNCTRL                0x14
#define XHCI_OPER_REG_CRCR_LO               0x18
#define   XHCI_CRCR_RCS                     (1 << 0)
#define   XHCI_CRCR_CS                      (1 << 1)
#define   XHCI_CRCR_CA                      (1 << 2)
#define   XHCI_CRCR_CRR                     (1 << 3)
#define XHCI_OPER_REG_CRCR_HI               0x1C
#define XHCI_OPER_REG_DCBAAP_LO             0x30
#define XHCI_OPER_REG_DCBAAP_HI             0x34
#define XHCI_OPER_REG_CONFIG                0x38

#define XHCI_DBELL_DB_SZ                    0x4

#define XHCI_INTR_REG_MFINDEX               0x00
#define XHCI_INTR_REG_IR0                   0x20
#define XHCI_INTR_IR_SZ                     0x20

#define XHCI_INTR_REG_IMAN                  0x00
#define   XHCI_IMAN_IP                      (1 << 0)
#define   XHCI_IMAN_IE                      (1 << 1)
#define XHCI_INTR_REG_IMOD                  0x04
#define XHCI_INTR_REG_ERSTSZ                0x08
#define XHCI_INTR_REG_ERSTBA_LO             0x10
#define XHCI_INTR_REG_ERSTBA_HI             0x14
#define XHCI_INTR_REG_ERDP_LO               0x18
#define   XHCI_ERDP_EHB                     (1 << 3)
#define XHCI_INTR_REG_ERDP_HI               0x1C

#define TRB_SIZE 16
typedef struct XHCITRB {
    uint64_t parameter;
    uint32_t status;
    uint32_t control;
    dma_addr_t addr;
    bool ccs;
} XHCITRB;

enum {
    PLS_U0              =  0,
    PLS_U1              =  1,
    PLS_U2              =  2,
    PLS_U3              =  3,
    PLS_DISABLED        =  4,
    PLS_RX_DETECT       =  5,
    PLS_INACTIVE        =  6,
    PLS_POLLING         =  7,
    PLS_RECOVERY        =  8,
    PLS_HOT_RESET       =  9,
    PLS_COMPILANCE_MODE = 10,
    PLS_TEST_MODE       = 11,
    PLS_RESUME          = 15,
};

#define CR_LINK TR_LINK

#define TRB_C               (1 << 0)
#define TRB_TYPE_SHIFT      10
#define TRB_TYPE_MASK       0x3f
#define TRB_TYPE(t)         (((t).control >> TRB_TYPE_SHIFT) & TRB_TYPE_MASK)

#define TRB_EV_ED           (1 << 2)

#define TRB_TR_ENT          (1 << 1)
#define TRB_TR_ISP          (1 << 2)
#define TRB_TR_NS           (1 << 3)
#define TRB_TR_CH           (1 << 4)
#define TRB_TR_IOC          (1 << 5)
#define TRB_TR_IDT          (1 << 6)
#define TRB_TR_TBC_SHIFT    7
#define TRB_TR_TBC_MASK     0x3
#define TRB_TR_BEI          (1 << 9)
#define TRB_TR_TLBPC_SHIFT  16
#define TRB_TR_TLBPC_MASK   0xf
#define TRB_TR_FRAMEID_SHIFT    20
#define TRB_TR_FRAMEID_MASK 0x7ff
#define TRB_TR_SIA          (1 << 31)

#define TRB_TR_DIR          (1 << 16)

#define TRB_CR_SLOTID_SHIFT 24
#define TRB_CR_SLOTID_MASK  0xff
#define TRB_CR_EPID_SHIFT   16
#define TRB_CR_EPID_MASK    0x1f

#define TRB_CR_BSR          (1 << 9)
#define TRB_CR_DC           (1 << 9)

#define TRB_LK_TC           (1 << 1)

#define TRB_INTR_SHIFT      22
#define TRB_INTR_MASK       0x3ff
#define TRB_INTR(t)         (((t).status >> TRB_INTR_SHIFT) & TRB_INTR_MASK)

#define EP_TYPE_MASK        0x7
#define EP_TYPE_SHIFT       3

#define EP_STATE_MASK       0x7
#define EP_DISABLED         (0 << 0)
#define EP_RUNNING          (1 << 0)
#define EP_HALTED           (2 << 0)
#define EP_STOPPED          (3 << 0)
#define EP_ERROR            (4 << 0)

#define SLOT_STATE_MASK     0x1f
#define SLOT_STATE_SHIFT    27
#define SLOT_STATE(s)       (((s) >> SLOT_STATE_SHIFT) & SLOT_STATE_MASK)
#define SLOT_ENABLED        0
#define SLOT_DEFAULT        1
#define SLOT_ADDRESSED      2
#define SLOT_CONFIGURED     3

#define SLOT_CONTEXT_ENTRIES_MASK 0x1f
#define SLOT_CONTEXT_ENTRIES_SHIFT 27

typedef enum EPType {
    ET_INVALID = 0,
    ET_ISO_OUT,
    ET_BULK_OUT,
    ET_INTR_OUT,
    ET_CONTROL,
    ET_ISO_IN,
    ET_BULK_IN,
    ET_INTR_IN,
} EPType;

typedef struct XHCIRing {
    dma_addr_t dequeue;
    bool ccs;
} XHCIRing;

typedef struct XHCIPort {
    XHCIState *xhci;
    uint32_t portsc;
    uint32_t portnr;
    USBPort  *uport;
    uint32_t speedmask;
    char name[20];
    MemoryRegion mem;
} XHCIPort;

typedef struct XHCISlot {
    bool enabled;
    bool addressed;
    uint16_t intr;
    dma_addr_t ctx;
    USBPort *uport;
    XHCIEPContext *eps[31];
} XHCISlot;

typedef struct XHCIEvent {
    TRBType type;
    TRBCCode ccode;
    uint64_t ptr;
    uint32_t length;
    uint32_t flags;
    uint8_t slotid;
    uint8_t epid;
} XHCIEvent;

typedef struct XHCIInterrupter {
    uint32_t iman;
    uint32_t imod;
    uint32_t erstsz;
    uint32_t erstba_low;
    uint32_t erstba_high;
    uint32_t erdp_low;
    uint32_t erdp_high;

    bool msix_used, er_pcs;

    dma_addr_t er_start;
    uint32_t er_size;
    unsigned int er_ep_idx;

    /* kept for live migration compat only */
    bool er_full_unused;
    XHCIEvent ev_buffer[EV_QUEUE];
    unsigned int ev_buffer_put;
    unsigned int ev_buffer_get;

} XHCIInterrupter;

typedef struct XHCIState {
    DeviceState parent;

    USBBus bus;
    MemoryRegion mem;
    MemoryRegion *dma_mr;
    AddressSpace *as;
    MemoryRegion mem_cap;
    MemoryRegion mem_oper;
    MemoryRegion mem_runtime;
    MemoryRegion mem_doorbell;

    /* properties */
    uint32_t numports_2;
    uint32_t numports_3;
    uint32_t numintrs;
    uint32_t numslots;
    uint32_t flags;
    uint32_t max_pstreams_mask;
    void (*intr_update)(XHCIState *s, int n, bool enable);
    bool (*intr_raise)(XHCIState *s, int n, bool level);
    /*
     * Callback for special-casing interrupter mapping support. NULL for most
     * implementations, for defaulting to enabled mapping unless numintrs == 1.
     */
    bool (*intr_mapping_supported)(XHCIState *s);
    DeviceState *hostOpaque;

    /* Operational Registers */
    uint32_t usbcmd;
    uint32_t usbsts;
    uint32_t dnctrl;
    uint32_t crcr_low;
    uint32_t crcr_high;
    uint32_t dcbaap_low;
    uint32_t dcbaap_high;
    uint32_t config;

    USBPort  uports[MAX_CONST(XHCI_MAXPORTS_2, XHCI_MAXPORTS_3)];
    XHCIPort ports[XHCI_MAXPORTS];
    XHCISlot slots[XHCI_MAXSLOTS];
    uint32_t numports;

    /* Runtime Registers */
    int64_t mfindex_start;
    QEMUTimer *mfwrap_timer;
    XHCIInterrupter intr[XHCI_MAXINTRS];

    XHCIRing cmd_ring;

    bool nec_quirks;
} XHCIState;

extern const VMStateDescription vmstate_xhci;
bool xhci_get_flag(XHCIState *xhci, enum xhci_flags bit);
void xhci_set_flag(XHCIState *xhci, enum xhci_flags bit);
#endif
