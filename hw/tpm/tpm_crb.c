/*
 * tpm_crb.c - QEMU's TPM CRB interface emulator
 *
 * Copyright (c) 2017 Red Hat, Inc.
 *
 * Authors:
 *   Marc-André Lureau <marcandre.lureau@redhat.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 *
 * tpm_crb is a device for TPM 2.0 Command Response Buffer (CRB) Interface
 * as defined in TCG PC Client Platform TPM Profile (PTP) Specification
 * Family “2.0” Level 00 Revision 01.03 v22
 */

#include "qemu/osdep.h"

#include "qemu-common.h"
#include "qapi/error.h"
#include "hw/sysbus.h"
#include "exec/address-spaces.h"

#include "hw/pci/pci_ids.h"
#include "hw/acpi/tpm.h"
#include "sysemu/tpm_backend.h"
#include "tpm_int.h"
#include "tpm_util.h"

typedef struct CRBState {
    SysBusDevice parent_obj;

    TPMBackend *tpmbe;
    TPMBackendCmd cmd;
    struct crb_regs regs;
    MemoryRegion mmio;
    MemoryRegion cmdmem;

    size_t be_buffer_size;
} CRBState;

#define CRB(obj) OBJECT_CHECK(CRBState, (obj), TYPE_TPM_CRB)

#define DEBUG_CRB 0

#define DPRINTF(fmt, ...) do {                  \
        if (DEBUG_CRB) {                        \
            printf(fmt, ## __VA_ARGS__);        \
        }                                       \
    } while (0)

#define CRB_ADDR_LOC_STATE offsetof(struct crb_regs, loc_state)
#define CRB_ADDR_LOC_CTRL offsetof(struct crb_regs, loc_ctrl)
#define CRB_ADDR_CTRL_REQ offsetof(struct crb_regs, ctrl_req)
#define CRB_ADDR_CTRL_CANCEL offsetof(struct crb_regs, ctrl_cancel)
#define CRB_ADDR_CTRL_START offsetof(struct crb_regs, ctrl_start)

#define CRB_INTF_TYPE_CRB_ACTIVE 0b1
#define CRB_INTF_VERSION_CRB 0b1
#define CRB_INTF_CAP_LOCALITY_0_ONLY 0b0
#define CRB_INTF_CAP_IDLE_FAST 0b0
#define CRB_INTF_CAP_XFER_SIZE_64 0b11
#define CRB_INTF_CAP_FIFO_NOT_SUPPORTED 0b0
#define CRB_INTF_CAP_CRB_SUPPORTED 0b1
#define CRB_INTF_IF_SELECTOR_CRB 0b1
#define CRB_INTF_IF_SELECTOR_UNLOCKED 0b0

#define CRB_CTRL_CMD_SIZE (TPM_CRB_ADDR_SIZE - sizeof(struct crb_regs))

enum crb_loc_ctrl {
    CRB_LOC_CTRL_REQUEST_ACCESS = BIT(0),
    CRB_LOC_CTRL_RELINQUISH = BIT(1),
    CRB_LOC_CTRL_SEIZE = BIT(2),
    CRB_LOC_CTRL_RESET_ESTABLISHMENT_BIT = BIT(3),
};

enum crb_ctrl_req {
    CRB_CTRL_REQ_CMD_READY = BIT(0),
    CRB_CTRL_REQ_GO_IDLE = BIT(1),
};

enum crb_start {
    CRB_START_INVOKE = BIT(0),
};

enum crb_cancel {
    CRB_CANCEL_INVOKE = BIT(0),
};

static const char *addr_desc(unsigned off)
{
    struct crb_regs crb;

    switch (off) {
#define CASE(field)                                                 \
    case offsetof(struct crb_regs, field) ...                       \
        offsetof(struct crb_regs, field) + sizeof(crb.field) - 1:   \
        return G_STRINGIFY(field);
        CASE(loc_state);
        CASE(reserved1);
        CASE(loc_ctrl);
        CASE(loc_sts);
        CASE(reserved2);
        CASE(intf_id);
        CASE(ctrl_ext);
        CASE(ctrl_req);
        CASE(ctrl_sts);
        CASE(ctrl_cancel);
        CASE(ctrl_start);
        CASE(ctrl_int_enable);
        CASE(ctrl_int_sts);
        CASE(ctrl_cmd_size);
        CASE(ctrl_cmd_pa_low);
        CASE(ctrl_cmd_pa_high);
        CASE(ctrl_rsp_size);
        CASE(ctrl_rsp_pa);
#undef CASE
    }
    return NULL;
}

static uint64_t tpm_crb_mmio_read(void *opaque, hwaddr addr,
                                  unsigned size)
{
    CRBState *s = CRB(opaque);
    DPRINTF("CRB read 0x%lx:%s len:%u\n",
            addr, addr_desc(addr), size);
    void *regs = (void *)&s->regs + addr;

    switch (size) {
    case 1:
        return *(uint8_t *)regs;
    case 2:
        return *(uint16_t *)regs;
    case 4:
        return *(uint32_t *)regs;
    default:
        g_return_val_if_reached(-1);
    }
}

static void tpm_crb_mmio_write(void *opaque, hwaddr addr,
                               uint64_t val, unsigned size)
{
    CRBState *s = CRB(opaque);
    DPRINTF("CRB write 0x%lx:%s len:%u val:%lu\n",
            addr, addr_desc(addr), size, val);

    switch (addr) {
    case CRB_ADDR_CTRL_REQ:
        switch (val) {
        case CRB_CTRL_REQ_CMD_READY:
            s->regs.ctrl_sts.bits.tpm_idle = 0;
            break;
        case CRB_CTRL_REQ_GO_IDLE:
            s->regs.ctrl_sts.bits.tpm_idle = 1;
            break;
        }
        break;
    case CRB_ADDR_CTRL_CANCEL:
        if (val == CRB_CANCEL_INVOKE && s->regs.ctrl_start & CRB_START_INVOKE) {
            tpm_backend_cancel_cmd(s->tpmbe);
        }
        break;
    case CRB_ADDR_CTRL_START:
        if (val == CRB_START_INVOKE &&
            !(s->regs.ctrl_start & CRB_START_INVOKE)) {
            void *mem = memory_region_get_ram_ptr(&s->cmdmem);

            s->regs.ctrl_start |= CRB_START_INVOKE;
            s->cmd = (TPMBackendCmd) {
                .in = mem,
                .in_len = MIN(tpm_cmd_get_size(mem), s->be_buffer_size),
                .out = mem,
                .out_len = s->be_buffer_size,
            };

            tpm_backend_deliver_request(s->tpmbe, &s->cmd);
        }
        break;
    case CRB_ADDR_LOC_CTRL:
        switch (val) {
        case CRB_LOC_CTRL_RESET_ESTABLISHMENT_BIT:
            /* not loc 3 or 4 */
            break;
        case CRB_LOC_CTRL_RELINQUISH:
            break;
        case CRB_LOC_CTRL_REQUEST_ACCESS:
            s->regs.loc_sts.bits.granted = 1;
            s->regs.loc_sts.bits.been_seized = 0;
            s->regs.loc_state.bits.loc_assigned = 1;
            s->regs.loc_state.bits.tpm_reg_valid_sts = 1;
            break;
        }
        break;
    }
}

static const MemoryRegionOps tpm_crb_memory_ops = {
    .read = tpm_crb_mmio_read,
    .write = tpm_crb_mmio_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = {
        .min_access_size = 1,
        .max_access_size = 4,
    },
};

static void tpm_crb_reset(DeviceState *dev)
{
    CRBState *s = CRB(dev);

    tpm_backend_reset(s->tpmbe);

    s->be_buffer_size = MIN(tpm_backend_get_buffer_size(s->tpmbe),
                            CRB_CTRL_CMD_SIZE);

    s->regs = (struct crb_regs) {
        .intf_id.bits = {
            .type = CRB_INTF_TYPE_CRB_ACTIVE,
            .version = CRB_INTF_VERSION_CRB,
            .cap_locality = CRB_INTF_CAP_LOCALITY_0_ONLY,
            .cap_crb_idle_bypass = CRB_INTF_CAP_IDLE_FAST,
            .cap_data_xfer_size_support = CRB_INTF_CAP_XFER_SIZE_64,
            .cap_fifo = CRB_INTF_CAP_FIFO_NOT_SUPPORTED,
            .cap_crb = CRB_INTF_CAP_CRB_SUPPORTED,
            .cap_if_res = 0b0,
            .if_selector = CRB_INTF_IF_SELECTOR_CRB,
            .if_selector_lock = CRB_INTF_IF_SELECTOR_UNLOCKED,
            .rid = 0b0001,
            .vid = PCI_VENDOR_ID_IBM,
            .did = 0b0001,
        },
        .ctrl_cmd_size = CRB_CTRL_CMD_SIZE,
        .ctrl_cmd_pa_low = TPM_CRB_ADDR_BASE + sizeof(struct crb_regs),
        .ctrl_rsp_size = CRB_CTRL_CMD_SIZE,
        .ctrl_rsp_pa = TPM_CRB_ADDR_BASE + sizeof(struct crb_regs),
    };

    tpm_backend_startup_tpm(s->tpmbe, s->be_buffer_size);
}

static void tpm_crb_request_completed(TPMIf *ti, int ret)
{
    CRBState *s = CRB(ti);

    s->regs.ctrl_start &= ~CRB_START_INVOKE;
    if (ret != 0) {
        s->regs.ctrl_sts.bits.tpm_sts = 1; /* fatal error */
    }
}

static enum TPMVersion tpm_crb_get_version(TPMIf *ti)
{
    CRBState *s = CRB(ti);

    return tpm_backend_get_tpm_version(s->tpmbe);
}

/* persistent state handling */

static int tpm_crb_pre_save(void *opaque)
{
    CRBState *s = opaque;

    tpm_backend_finish_sync(s->tpmbe);

    return 0;
}

static const VMStateDescription vmstate_tpm_crb = {
    .name = "tpm-crb",
    .pre_save  = tpm_crb_pre_save,
    .fields      = (VMStateField[]) {
        VMSTATE_UINT32(regs.loc_state.reg, CRBState),
        VMSTATE_UINT32(regs.loc_ctrl, CRBState),
        VMSTATE_UINT32(regs.loc_sts.reg, CRBState),
        VMSTATE_UINT64(regs.intf_id.reg, CRBState),
        VMSTATE_UINT64(regs.ctrl_ext, CRBState),
        VMSTATE_UINT32(regs.ctrl_req, CRBState),
        VMSTATE_UINT32(regs.ctrl_sts.reg, CRBState),
        VMSTATE_UINT32(regs.ctrl_cancel, CRBState),
        VMSTATE_UINT32(regs.ctrl_start, CRBState),
        VMSTATE_UINT32(regs.ctrl_int_enable, CRBState),
        VMSTATE_UINT32(regs.ctrl_int_sts, CRBState),
        VMSTATE_UINT32(regs.ctrl_cmd_size, CRBState),
        VMSTATE_UINT32(regs.ctrl_cmd_pa_low, CRBState),
        VMSTATE_UINT32(regs.ctrl_rsp_size, CRBState),
        VMSTATE_UINT64(regs.ctrl_rsp_pa, CRBState),

        VMSTATE_END_OF_LIST(),
    }
};

static Property tpm_crb_properties[] = {
    DEFINE_PROP_TPMBE("tpmdev", CRBState, tpmbe),
    DEFINE_PROP_END_OF_LIST(),
};

static void tpm_crb_realizefn(DeviceState *dev, Error **errp)
{
    CRBState *s = CRB(dev);
    SysBusDevice *sbd = SYS_BUS_DEVICE(dev);

    if (!tpm_find()) {
        error_setg(errp, "at most one TPM device is permitted");
        return;
    }
    if (!s->tpmbe) {
        error_setg(errp, "'tpmdev' property is required");
        return;
    }

    memory_region_init_io(&s->mmio, OBJECT(s), &tpm_crb_memory_ops, s,
        "tpm-crb-mmio", sizeof(struct crb_regs));
    memory_region_init_ram(&s->cmdmem, OBJECT(s),
        "tpm-crb-cmd", CRB_CTRL_CMD_SIZE, errp);

    sysbus_init_mmio(sbd, &s->mmio);
    sysbus_mmio_map(sbd, 0, TPM_CRB_ADDR_BASE);
    /* allocate ram in bios instead? */
    memory_region_add_subregion(get_system_memory(),
        TPM_CRB_ADDR_BASE + sizeof(struct crb_regs), &s->cmdmem);
}

static void tpm_crb_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    TPMIfClass *tc = TPM_IF_CLASS(klass);

    dc->realize = tpm_crb_realizefn;
    dc->props = tpm_crb_properties;
    dc->reset = tpm_crb_reset;
    dc->vmsd  = &vmstate_tpm_crb;
    dc->user_creatable = true;
    tc->model = TPM_MODEL_TPM_CRB;
    tc->get_version = tpm_crb_get_version;
    tc->request_completed = tpm_crb_request_completed;
}

static const TypeInfo tpm_crb_info = {
    .name = TYPE_TPM_CRB,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(CRBState),
    .class_init  = tpm_crb_class_init,
    .interfaces = (InterfaceInfo[]) {
        { TYPE_TPM_IF },
        { }
    }
};

static void tpm_crb_register(void)
{
    type_register_static(&tpm_crb_info);
}

type_init(tpm_crb_register)
