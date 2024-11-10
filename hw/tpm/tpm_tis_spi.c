/*
 * QEMU SPI TPM 2.0 model
 *
 * Copyright (c) 2024, IBM Corporation.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "qemu/log.h"
#include "hw/sysbus.h"
#include "hw/acpi/tpm.h"
#include "tpm_prop.h"
#include "qemu/log.h"
#include "trace.h"
#include "tpm_tis.h"
#include "hw/ssi/ssi.h"
#include "migration/vmstate.h"

typedef struct TPMStateSPI {
    /*< private >*/
    SSIPeripheral parent_object;

    uint8_t     byte_offset;     /* byte offset in transfer */
    uint8_t     wait_state_cnt;  /* wait state counter */
    uint8_t     xfer_size;       /* data size of transfer */
    uint32_t    reg_addr;        /* register address of transfer */

    uint8_t     spi_state;       /* READ / WRITE / IDLE */
#define SPI_STATE_IDLE   0
#define SPI_STATE_WRITE  1
#define SPI_STATE_READ   2

    bool        command;

    /*< public >*/
    TPMState    tpm_state;       /* not a QOM object */

} TPMStateSPI;

#define CMD_BYTE_WRITE          (1 << 7)
#define CMD_BYTE_XFER_SZ_MASK   0x1f
#define TIS_SPI_HIGH_ADDR_BYTE  0xd4
#define NUM_WAIT_STATES         1

DECLARE_INSTANCE_CHECKER(TPMStateSPI, TPM_TIS_SPI, TYPE_TPM_TIS_SPI)

static int tpm_tis_spi_pre_save(void *opaque)
{
    TPMStateSPI *spist = opaque;

    return tpm_tis_pre_save(&spist->tpm_state);
}

static const VMStateDescription vmstate_tpm_tis_spi = {
     .name = "tpm-tis-spi",
     .version_id = 0,
     .pre_save  = tpm_tis_spi_pre_save,
     .fields = (const VMStateField[]) {
         VMSTATE_BUFFER(tpm_state.buffer, TPMStateSPI),
         VMSTATE_UINT16(tpm_state.rw_offset, TPMStateSPI),
         VMSTATE_UINT8(tpm_state.active_locty, TPMStateSPI),
         VMSTATE_UINT8(tpm_state.aborting_locty, TPMStateSPI),
         VMSTATE_UINT8(tpm_state.next_locty, TPMStateSPI),

         VMSTATE_STRUCT_ARRAY(tpm_state.loc, TPMStateSPI,
                              TPM_TIS_NUM_LOCALITIES, 0,
                              vmstate_locty, TPMLocality),

         /* spi specifics */
         VMSTATE_UINT8(byte_offset, TPMStateSPI),
         VMSTATE_UINT8(wait_state_cnt, TPMStateSPI),
         VMSTATE_UINT8(xfer_size, TPMStateSPI),
         VMSTATE_UINT32(reg_addr, TPMStateSPI),
         VMSTATE_UINT8(spi_state, TPMStateSPI),
         VMSTATE_BOOL(command, TPMStateSPI),

         VMSTATE_END_OF_LIST()
     }
};

static inline void tpm_tis_spi_clear_data(TPMStateSPI *spist)
{
    spist->spi_state = SPI_STATE_IDLE;
    spist->byte_offset = 0;
    spist->wait_state_cnt = 0;
    spist->xfer_size = 0;
    spist->reg_addr = 0;

    return;
}

/* Callback from TPM to indicate that response is copied */
static void tpm_tis_spi_request_completed(TPMIf *ti, int ret)
{
    TPMStateSPI *spist = TPM_TIS_SPI(ti);
    TPMState *s = &spist->tpm_state;

    /* Inform the common code. */
    tpm_tis_request_completed(s, ret);
}

static enum TPMVersion tpm_tis_spi_get_tpm_version(TPMIf *ti)
{
    TPMStateSPI *spist = TPM_TIS_SPI(ti);
    TPMState *s = &spist->tpm_state;

    return tpm_tis_get_tpm_version(s);
}

/*
 * TCG PC Client Platform TPM Profile Specification for TPM 2.0 ver 1.05 rev 14
 *
 * For system Software, the TPM has a 64-bit address of 0x0000_0000_FED4_xxxx.
 * On SPI, the chipset passes the least significant 24 bits to the TPM.
 * The upper bytes will be used by the chipset to select the TPM’s SPI CS#
 * signal. Table 9 shows the locality based on the 16 least significant address
 * bits and assume that either the LPC TPM sync or SPI TPM CS# is used.
 *
 */
static void tpm_tis_spi_write(TPMStateSPI *spist, uint32_t addr, uint8_t val)
{
    TPMState *tpm_st = &spist->tpm_state;

    trace_tpm_tis_spi_write(addr, val);
    tpm_tis_write_data(tpm_st, addr, val, 1);
}

static uint8_t tpm_tis_spi_read(TPMStateSPI *spist, uint32_t addr)
{
    TPMState *tpm_st = &spist->tpm_state;
    uint8_t data;

    data = tpm_tis_read_data(tpm_st, addr, 1);
    trace_tpm_tis_spi_read(addr, data);
    return data;
}

static Property tpm_tis_spi_properties[] = {
    DEFINE_PROP_TPMBE("tpmdev", TPMStateSPI, tpm_state.be_driver),
    DEFINE_PROP_END_OF_LIST(),
};

static void tpm_tis_spi_reset(DeviceState *dev)
{
    TPMStateSPI *spist = TPM_TIS_SPI(dev);
    TPMState *s = &spist->tpm_state;

    tpm_tis_spi_clear_data(spist);
    return tpm_tis_reset(s);
}

static uint32_t tpm_tis_spi_transfer(SSIPeripheral *ss, uint32_t tx)
{
    TPMStateSPI *spist = TPM_TIS_SPI(ss);
    uint32_t rx = 0;
    uint8_t byte;       /* reversed byte value */
    uint8_t offset = 0; /* offset of byte in payload */
    uint8_t index;      /* index of data byte in transfer */
    uint32_t tis_addr;  /* tis address including locty */

    /* new transfer or not */
    if (spist->command) {   /* new transfer start */
        if (spist->spi_state != SPI_STATE_IDLE) {
            qemu_log_mask(LOG_GUEST_ERROR, "unexpected new transfer\n");
        }
        spist->byte_offset = 0;
        spist->wait_state_cnt = 0;
    }
    /*
     * Explanation of wait_state:
     * The original TPM model did not have wait state or "flow control" support
     * built in.  If you wanted to read a TPM register through SPI you sent
     * the first byte with the read/write bit and size, then three address bytes
     * and any additional bytes after that were don't care bytes for reads and
     * the model would begin returning byte data to the SPI reader from the
     * register address provided.  In the real world this would mean that a
     * TPM device had only the time between the 31st clock and the 32nd clock
     * to fetch the register data that it had to provide to SPI MISO starting
     * with the 32nd clock.
     *
     * In reality the TPM begins introducing a wait state at the 31st clock
     * by holding MISO low.  This is how it controls the "flow" of the
     * operation. Once the data the TPM needs to return is ready it will
     * select bit 31 + (8*N) to send back a 1 which indicates that it will
     * now start returning data on MISO.
     *
     * The same wait states are applied to writes.  In either the read or write
     * case the wait state occurs between the command+address (4 bytes) and the
     * data (1-n bytes) sections of the SPI frame.  The code below introduces
     * the support for a 32 bit wait state for P10.  All reads and writes
     * through the SPI interface MUST now be aware of the need to do flow
     * control in order to use the TPM via SPI.
     *
     * In conjunction with these changes there were changes made to the SPIM
     * engine that was introduced in P10 to support the 6x op code which is
     * used to receive wait state 0s on the MISO line until it sees the b'1'
     * come back before continuing to read real data from the SPI device(TPM).
     */

    trace_tpm_tis_spi_transfer_data("Payload byte_offset", spist->byte_offset);
    /* process payload data */
    while (offset < 4) {
        spist->command = false;
        byte = (tx >> (24 - 8 * offset)) & 0xFF;
        trace_tpm_tis_spi_transfer_data("Extracted byte", byte);
        trace_tpm_tis_spi_transfer_data("Payload offset", offset);
        switch (spist->byte_offset) {
        case 0:    /* command byte */
            if ((byte & CMD_BYTE_WRITE) == 0) {  /* bit-7 */
                spist->spi_state = SPI_STATE_WRITE;
                trace_tpm_tis_spi_transfer_event("spi write");
            } else {
                spist->spi_state = SPI_STATE_READ;
                trace_tpm_tis_spi_transfer_event("spi read");
            }
            spist->xfer_size = (byte & CMD_BYTE_XFER_SZ_MASK) + 1;
            trace_tpm_tis_spi_transfer_data("xfer_size", spist->xfer_size);
            break;
        case 1:     /* 1st address byte */
            if (byte != TIS_SPI_HIGH_ADDR_BYTE) {
                qemu_log_mask(LOG_GUEST_ERROR, "incorrect high address 0x%x\n",
                              byte);
            }
            spist->reg_addr = byte << 16;
            trace_tpm_tis_spi_transfer_data("first addr byte", byte);
            trace_tpm_tis_spi_transfer_addr("reg_addr", spist->reg_addr);
            break;
        case 2:     /* 2nd address byte */
            spist->reg_addr |= byte << 8;
            trace_tpm_tis_spi_transfer_data("second addr byte", byte);
            trace_tpm_tis_spi_transfer_addr("reg_addr", spist->reg_addr);
            break;
        case 3:     /* 3rd address byte */
            spist->reg_addr |= byte;
            trace_tpm_tis_spi_transfer_data("third addr byte", byte);
            trace_tpm_tis_spi_transfer_addr("reg_addr", spist->reg_addr);
            break;
        default:    /* data bytes */
            if (spist->wait_state_cnt < NUM_WAIT_STATES) {
                spist->wait_state_cnt++;
                if (spist->wait_state_cnt == NUM_WAIT_STATES) {
                    trace_tpm_tis_spi_transfer_data("wait complete, count",
                                                     spist->wait_state_cnt);
                    rx = rx | (0x01 << (24 - offset * 8));
                    return rx;
                } else {
                    trace_tpm_tis_spi_transfer_data("in wait state, count",
                                                     spist->wait_state_cnt);
                    rx = 0;
                }
            } else {
                index = spist->byte_offset - 4;
                trace_tpm_tis_spi_transfer_data("index", index);
                trace_tpm_tis_spi_transfer_data("data byte", byte);
                trace_tpm_tis_spi_transfer_addr("reg_addr", spist->reg_addr);
                if (index >= spist->xfer_size) {
                    /*
                     * SPI SSI framework limits both rx and tx
                     * to fixed 4-byte with each xfer
                     */
                    trace_tpm_tis_spi_transfer_event("index exceeds xfer_size");
                    return rx;
                }
                tis_addr = spist->reg_addr + (index % 4);
                if (spist->spi_state == SPI_STATE_WRITE) {
                    tpm_tis_spi_write(spist, tis_addr, byte);
                } else {
                    byte = tpm_tis_spi_read(spist, tis_addr);
                    rx = rx | (byte << (24 - offset * 8));
                    trace_tpm_tis_spi_transfer_data("byte added to response",
                                                     byte);
                    trace_tpm_tis_spi_transfer_data("offset", offset);
                }
            }
            break;
        }
        if ((spist->wait_state_cnt == 0) ||
            (spist->wait_state_cnt == NUM_WAIT_STATES)) {
            offset++;
            spist->byte_offset++;
        } else {
            break;
        }
    }
    return rx;
}

static int tpm_tis_spi_cs(SSIPeripheral *ss, bool select)
{
    TPMStateSPI *spist = TPM_TIS_SPI(ss);

    if (select) {
        spist->command = false;
        spist->spi_state = SPI_STATE_IDLE;
    } else {
        spist->command = true;
    }
    return 0;
}

static void tpm_tis_spi_realize(SSIPeripheral *dev, Error **errp)
{
    TPMStateSPI *spist = TPM_TIS_SPI(dev);
    TPMState *s = &spist->tpm_state;

    if (!tpm_find()) {
        error_setg(errp, "at most one TPM device is permitted");
        return;
    }

    s->be_driver = qemu_find_tpm_be("tpm0");

    if (!s->be_driver) {
        error_setg(errp, "unable to find tpm backend device");
        return;
    }
}

static void tpm_tis_spi_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    TPMIfClass *tc = TPM_IF_CLASS(klass);
    SSIPeripheralClass *k = SSI_PERIPHERAL_CLASS(klass);

    k->transfer = tpm_tis_spi_transfer;
    k->realize = tpm_tis_spi_realize;
    k->set_cs = tpm_tis_spi_cs;
    k->cs_polarity = SSI_CS_LOW;

    device_class_set_legacy_reset(dc, tpm_tis_spi_reset);
    device_class_set_props(dc, tpm_tis_spi_properties);
    set_bit(DEVICE_CATEGORY_MISC, dc->categories);

    dc->desc = "SPI TPM";
    dc->vmsd = &vmstate_tpm_tis_spi;

    tc->model = TPM_MODEL_TPM_TIS;
    tc->request_completed = tpm_tis_spi_request_completed;
    tc->get_version = tpm_tis_spi_get_tpm_version;
}

static const TypeInfo tpm_tis_spi_info = {
    .name          = TYPE_TPM_TIS_SPI,
    .parent        = TYPE_SSI_PERIPHERAL,
    .instance_size = sizeof(TPMStateSPI),
    .class_init    = tpm_tis_spi_class_init,
    .interfaces    = (InterfaceInfo[]) {
        { TYPE_TPM_IF },
        { }
    }
};

static void tpm_tis_spi_register_types(void)
{
    type_register_static(&tpm_tis_spi_info);
}

type_init(tpm_tis_spi_register_types)
