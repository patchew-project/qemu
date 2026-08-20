/*
 * Minimal NXP EdgeLock Enclave (ELE) responder stub
 *
 * Copyright (c) 2026, Kyle Fox
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * Watches TR-register writes on an i.MX MU, accumulates ELE-protocol
 * command words until the per-message size is reached, dispatches to a
 * command handler, and writes the response into RR registers + asserts
 * RSR.RFn so the agent's mu_hal_receivemsg() poll exits.
 *
 * ELE message format (per ele_api.h):
 *
 *   word 0 (header): version[7:0] | size[15:8] | command[23:16] | tag[31:24]
 *   word 1..N-1:     payload
 *
 * size = total words including header. tag = ELE_CMD_TAG (0x17) for
 * commands, ELE_RESP_TAG (0xE1) for responses. Responses also have a
 * status byte (ELE_SUCCESS_IND = 0xD6 for success) somewhere in the
 * payload depending on command.
 *
 * Only ELE_GET_INFO_REQ is implemented in detail (the
 * only ELE call U-Boot SPL's imx9_probe_mu() makes pre-relocation,
 * via ele_get_info()). Other commands get a generic SUCCESS response.
 */

#include "qemu/osdep.h"
#include "qemu/log.h"
#include "qemu/main-loop.h"
#include "qemu/module.h"
#include "qapi/error.h"
#include "hw/misc/imx95_ele_server.h"
#include "hw/core/qdev-properties.h"
#include "hw/core/qdev-properties-system.h"
#include "system/dma.h"
#include "migration/vmstate.h"
#include "trace.h"

/*
 * struct ele_get_info_data layout (u32 word offsets), pinned to the
 * U-Boot ele_api.h definition at
 * U-Boot arch/arm/include/asm/mach-imx/ele_api.h:168.
 * Using named offsets here so a future U-Boot rev that reorders or
 * adds fields trips a build/runtime mismatch instead of silently
 * writing into the wrong slot.
 *
 * Reference layout:
 *   u32 hdr;                  // word 0
 *   u32 soc;                  // word 1
 *   u32 lc;                   // word 2
 *   u32 uid[4];               // words 3..6
 *   u32 sha256_rom_patch[8];  // words 7..14
 *   u32 sha_fw[8];            // words 15..22
 *   u32 oem_srkh[16];         // words 23..38
 *   u32 state;                // word 39
 *   u32 oem_pqc_srkh[16];     // words 40..55
 *   u32 reserved[8];          // words 56..63
 * Total: 64 u32 = 256 bytes.
 */
#define ELE_INFO_OFFSET_HDR             0
#define ELE_INFO_OFFSET_SOC             1
#define ELE_INFO_OFFSET_LC              2
#define ELE_INFO_OFFSET_UID             3
#define ELE_INFO_OFFSET_SHA256_ROM      7
#define ELE_INFO_OFFSET_SHA_FW          15
#define ELE_INFO_OFFSET_OEM_SRKH        23
#define ELE_INFO_OFFSET_STATE           39
#define ELE_INFO_OFFSET_OEM_PQC_SRKH    40
#define ELE_INFO_OFFSET_RESERVED        56
#define ELE_INFO_SIZE_WORDS             64
#define ELE_INFO_SIZE_BYTES             (ELE_INFO_SIZE_WORDS * 4)

/* Header field packing. */
static inline uint32_t ele_make_header(uint8_t version, uint8_t size,
                                       uint8_t command, uint8_t tag)
{
    return (uint32_t)version |
           ((uint32_t)size    << 8) |
           ((uint32_t)command << 16) |
           ((uint32_t)tag     << 24);
}

/* Header field extraction. */
#define ELE_HDR_VERSION(h)  ((uint8_t)((h) & 0xFF))
#define ELE_HDR_SIZE(h)     ((uint8_t)(((h) >> 8) & 0xFF))
#define ELE_HDR_COMMAND(h)  ((uint8_t)(((h) >> 16) & 0xFF))
#define ELE_HDR_TAG(h)      ((uint8_t)(((h) >> 24) & 0xFF))

/*
 * ELE_GET_INFO_REQ handler.
 *
 * Request layout (4 words):
 *   [0] header (command = ELE_GET_INFO_REQ)
 *   [1] info_addr_hi (upper 32 bits of guest pointer)
 *   [2] info_addr_lo (lower 32 bits of guest pointer)
 *   [3] size of ele_get_info_data
 *
 * Real ELE writes a struct ele_get_info_data to the guest address.
 * We fake plausible values: rev 0xA1 i.MX 95, OEM-open lifecycle,
 * UID zero-filled. The Linux/U-Boot drivers do not validate beyond
 * memcpy into gd->arch.
 *
 * Response: 2 words. Status word ELE_SUCCESS_IND in data[0].
 */
static void ele_handle_get_info(IMX95ELEServerState *s)
{
    if (s->msg_count < 4) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: GET_INFO with only %u words received\n",
                      __func__, s->msg_count);
        return;
    }

    uint64_t info_addr = ((uint64_t)s->msg_buf[1] << 32) |
                          (uint64_t)s->msg_buf[2];

    /*
     * Zero-fill the full 256-byte struct, then set the fields U-Boot's
     * set_cpu_info() reads (soc, lc). Named offsets above keep the
     * mapping legible; if U-Boot ever shifts the struct layout, the
     * build check below + a re-read of ele_api.h are how this gets
     * detected.
     */
    uint32_t info_data[ELE_INFO_SIZE_WORDS] = {0};
    QEMU_BUILD_BUG_ON(sizeof(info_data) != ELE_INFO_SIZE_BYTES);

    info_data[ELE_INFO_OFFSET_SOC] = 0xA1009500;  /* SoC rev 0xA1, type 0x95 */
    info_data[ELE_INFO_OFFSET_LC]  = 0x00000080;  /* lifecycle = OEM open */

    dma_memory_write(&address_space_memory, info_addr,
                     info_data, sizeof(info_data),
                     MEMTXATTRS_UNSPECIFIED);

    /* Response: header + status. */
    uint32_t resp_hdr = ele_make_header(ELE_VERSION, 2,
                                        ELE_GET_INFO_REQ, ELE_RESP_TAG);
    trace_imx95_ele_response(ELE_GET_INFO_REQ);
    imx_mu_deliver_rr(s->mu, 0, resp_hdr);
    imx_mu_deliver_rr(s->mu, 1, ELE_SUCCESS_IND);
}

/*
 * Generic SUCCESS response for any command we do not specifically
 * handle. 2-word response: header + ELE_SUCCESS_IND.
 */
static void ele_handle_generic_ok(IMX95ELEServerState *s, uint8_t command)
{
    uint32_t resp_hdr = ele_make_header(ELE_VERSION, 2,
                                        command, ELE_RESP_TAG);
    trace_imx95_ele_response(command);
    imx_mu_deliver_rr(s->mu, 0, resp_hdr);
    imx_mu_deliver_rr(s->mu, 1, ELE_SUCCESS_IND);
}

static void ele_dispatch(IMX95ELEServerState *s)
{
    uint32_t header  = s->msg_buf[0];
    uint8_t  command = ELE_HDR_COMMAND(header);
    uint8_t  tag     = ELE_HDR_TAG(header);

    trace_imx95_ele_msg(command, tag, s->msg_size);

    switch (command) {
    case ELE_GET_INFO_REQ:
        ele_handle_get_info(s);
        return;
    default:
        qemu_log_mask(LOG_UNIMP,
                      "%s: cmd 0x%02x not handled in detail; responding OK\n",
                      __func__, command);
        ele_handle_generic_ok(s, command);
        return;
    }
}

/*
 * TR-write callback. Invoked synchronously by the MU model from inside
 * the guest's MMIO write to TR[idx]. The MU itself re-sets TSR.TEn
 * after this returns so the next TR write succeeds without polling.
 */
static void ele_on_tr_write(void *opaque, unsigned int idx, uint32_t value)
{
    IMX95ELEServerState *s = opaque;

    /* Invoked from the MU MMIO write handler, which runs under the BQL. */
    assert(bql_locked());

    if (s->msg_count >= IMX95_ELE_MAX_WORDS) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: too many words; resetting accumulator\n",
                      __func__);
        s->msg_count = 0;
        s->msg_size  = 0;
        return;
    }

    s->msg_buf[s->msg_count++] = value;

    if (s->msg_count == 1) {
        /* First word is the header; pick out size. */
        s->msg_size = ELE_HDR_SIZE(value);
        if (s->msg_size == 0 || s->msg_size > IMX95_ELE_MAX_WORDS) {
            qemu_log_mask(LOG_GUEST_ERROR,
                          "%s: bad size %u in header 0x%08x\n",
                          __func__, s->msg_size, value);
            s->msg_count = 0;
            s->msg_size  = 0;
            return;
        }
    }

    if (s->msg_count == s->msg_size) {
        ele_dispatch(s);
        s->msg_count = 0;
        s->msg_size  = 0;
    }
}

static void imx95_ele_server_reset_hold(Object *obj, ResetType type)
{
    IMX95ELEServerState *s = IMX95_ELE_SERVER(obj);

    s->msg_count = 0;
    s->msg_size  = 0;
    memset(s->msg_buf, 0, sizeof(s->msg_buf));
}

static void imx95_ele_server_realize(DeviceState *dev, Error **errp)
{
    IMX95ELEServerState *s = IMX95_ELE_SERVER(dev);

    if (!s->mu) {
        error_setg(errp, "%s: 'mu' link property must be set",
                   TYPE_IMX95_ELE_SERVER);
        return;
    }
    imx_mu_set_tr_write_handler(s->mu, ele_on_tr_write, s);
}

static const Property imx95_ele_server_properties[] = {
    DEFINE_PROP_LINK("mu", IMX95ELEServerState, mu,
                     TYPE_IMX_MU, IMXMUState *),
};

static const VMStateDescription vmstate_imx95_ele_server = {
    .name = TYPE_IMX95_ELE_SERVER,
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (const VMStateField[]) {
        VMSTATE_UINT32(msg_count, IMX95ELEServerState),
        VMSTATE_UINT32(msg_size, IMX95ELEServerState),
        VMSTATE_UINT32_ARRAY(msg_buf, IMX95ELEServerState,
                             IMX95_ELE_MAX_WORDS),
        VMSTATE_END_OF_LIST()
    },
};

static void imx95_ele_server_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    ResettableClass *rc = RESETTABLE_CLASS(klass);

    dc->realize = imx95_ele_server_realize;
    dc->vmsd = &vmstate_imx95_ele_server;
    rc->phases.hold = imx95_ele_server_reset_hold;
    set_bit(DEVICE_CATEGORY_MISC, dc->categories);
    dc->desc = "NXP i.MX 95 ELE responder (get_info)";
    device_class_set_props(dc, imx95_ele_server_properties);
}

static const TypeInfo imx95_ele_server_info = {
    .name           = TYPE_IMX95_ELE_SERVER,
    .parent         = TYPE_SYS_BUS_DEVICE,
    .instance_size  = sizeof(IMX95ELEServerState),
    .class_init     = imx95_ele_server_class_init,
};

static void imx95_ele_server_register_types(void)
{
    type_register_static(&imx95_ele_server_info);
}

type_init(imx95_ele_server_register_types)
