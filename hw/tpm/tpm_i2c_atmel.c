/*
 * tpm_i2c_atmel.c - QEMU's TPM I2C interface emulator
 *
 * Copyright (C) 2012, HPE Corporation
 *
 * Authors:
 *  Fabio Urquiza <fabio.urquiza@hpe.com>
 *
 * Based on tpm_tis.c:
 *  Stefan Berger <stefanb@us.ibm.com>
 *  David Safford <safford@us.ibm.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 *
 * Implementation of the TIS I2C interface according to specs found at
 * http://www.trustedcomputinggroup.org. This implementation currently
 * supports version 1.2 Atmel AT97SC3204T CI, 10 December 2016
 *
 * TPM I2C for TPM 2 implementation following TCG TPM I2C Interface
 * Specification TPM Profile (PTP) Specification, Familiy 2.0, Revision 1.0
 */


#include "qemu/osdep.h"
#include "qemu-common.h"
#include "qemu/main-loop.h"
#include "hw/i2c/i2c.h"
#include "qemu/bcd.h"
#include "sysemu/tpm_backend.h"
#include "tpm_int.h"
#include "qapi/error.h"

#define DEBUG_TIS 0

#define DPRINTF(fmt, ...) do { \
    if (DEBUG_TIS) { \
        printf(fmt, ## __VA_ARGS__); \
    } \
} while (0);

/* vendor-specific registers */
#define TPM_TIS_STS_TPM_FAMILY_MASK         (0x3 << 26)/* TPM 2.0 */
#define TPM_TIS_STS_TPM_FAMILY1_2           (0 << 26)  /* TPM 2.0 */
#define TPM_TIS_STS_TPM_FAMILY2_0           (1 << 26)  /* TPM 2.0 */

#define TPM_TIS_STS_VALID                 (1 << 7)
#define TPM_TIS_STS_DATA_AVAILABLE        (1 << 4)
#define TPM_TIS_STS_SELFTEST_DONE         (1 << 2)

#define TPM_TIS_ACCESS_TPM_REG_VALID_STS  (1 << 7)

#define TPM_TIS_IFACE_ID_INTERFACE_TIS1_3   (0xf)     /* TPM 2.0 */
#define TPM_TIS_IFACE_ID_INTERFACE_FIFO     (0x0)     /* TPM 2.0 */
#define TPM_TIS_IFACE_ID_INTERFACE_VER_FIFO (0 << 4)  /* TPM 2.0 */
#define TPM_TIS_IFACE_ID_CAP_5_LOCALITIES   (1 << 8)  /* TPM 2.0 */
#define TPM_TIS_IFACE_ID_CAP_TIS_SUPPORTED  (1 << 13) /* TPM 2.0 */
#define TPM_TIS_IFACE_ID_INT_SEL_LOCK       (1 << 19) /* TPM 2.0 */

#define TPM_TIS_IFACE_ID_SUPPORTED_FLAGS1_3 \
    (TPM_TIS_IFACE_ID_INTERFACE_TIS1_3 | \
     (~0u << 4)/* all of it is don't care */)

/* if backend was a TPM 2.0: */
#define TPM_TIS_IFACE_ID_SUPPORTED_FLAGS2_0 \
    (TPM_TIS_IFACE_ID_INTERFACE_FIFO | \
     TPM_TIS_IFACE_ID_INTERFACE_VER_FIFO | \
     TPM_TIS_IFACE_ID_CAP_5_LOCALITIES | \
     TPM_TIS_IFACE_ID_CAP_TIS_SUPPORTED)

#define TPM_TIS_NO_DATA_BYTE  0xff

static const VMStateDescription vmstate_tpm_i2c_atmel = {
    .name = "tpm",
    .unmigratable = 1,
};

static uint32_t tpm_i2c_atmel_get_size_from_buffer(const TPMSizedBuffer *sb)
{
    return be32_to_cpu(*(uint32_t *)&sb->buffer[2]);
}

static void tpm_i2c_atmel_show_buffer(const TPMSizedBuffer *sb, const char *string)
{
#ifdef DEBUG_TIS
    uint32_t len, i;

    len = tpm_i2c_atmel_get_size_from_buffer(sb);
    DPRINTF("tpm_tis: %s length = %d\n", string, len);
    for (i = 0; i < len; i++) {
        if (i && !(i % 16)) {
            DPRINTF("\n");
        }
        DPRINTF("%.2X ", sb->buffer[i]);
    }
    DPRINTF("\n");
#endif
}

/*
 * Set the given flags in the STS register by clearing the register but
 * preserving the SELFTEST_DONE and TPM_FAMILY_MASK flags and then setting
 * the new flags.
 *
 * The SELFTEST_DONE flag is acquired from the backend that determines it by
 * peeking into TPM commands.
 *
 * A VM suspend/resume will preserve the flag by storing it into the VM
 * device state, but the backend will not remember it when QEMU is started
 * again. Therefore, we cache the flag here. Once set, it will not be unset
 * except by a reset.
 */
static inline void tpm_i2c_atmel_sts_set(TPMLocality *l, uint32_t flags)
{
    l->sts &= TPM_TIS_STS_SELFTEST_DONE | TPM_TIS_STS_TPM_FAMILY_MASK;
    l->sts |= flags;
}

static inline uint32_t tpm_i2c_atmel_tpm_start_recv(TPMState *s)
{
    TPMTISEmuState *tis = &s->s.tis;
    tis->loc[0].r_offset = 0;

    return !(tis->loc[0].sts & TPM_TIS_STS_DATA_AVAILABLE);
}

static inline void tpm_i2c_atmel_tpm_start_send(TPMState *s)
{
    TPMTISEmuState *tis = &s->s.tis;
    tis->loc[0].r_offset = 0;
    tis->loc[0].w_offset = 0;
}

/*
 * Send a request to the TPM.
 */
static inline void tpm_i2c_atmel_tpm_send(TPMState *s)
{
    TPMTISEmuState *tis = &s->s.tis;

    if (tis->loc[0].w_offset &&
        tis->loc[0].state != TPM_TIS_STATE_EXECUTION) {
        tpm_i2c_atmel_show_buffer(&tis->loc[0].w_buffer, "tpm_tis: To TPM");

        s->locty_number = 0;
        s->locty_data = &tis->loc[0];

        /*
         * w_offset serves as length indicator for length of data;
         * it's reset when the response comes back
         */
        tis->loc[0].state = TPM_TIS_STATE_EXECUTION;

        tpm_backend_deliver_request(s->be_driver);
    }
}


static void tpm_i2c_atmel_receive_bh(void *opaque)
{
    TPMState *s = opaque;
    TPMTISEmuState *tis = &s->s.tis;

    tpm_i2c_atmel_sts_set(&tis->loc[0],
                    TPM_TIS_STS_VALID | TPM_TIS_STS_DATA_AVAILABLE);
    tis->loc[0].state = TPM_TIS_STATE_COMPLETION;
    tis->loc[0].r_offset = 0;
    tis->loc[0].w_offset = 0;
    DPRINTF("tpm_i2c_atmel: tpm_i2c_atmel_receive_bh");

}

/*
 * Read a byte of response data
 */
static inline uint32_t tpm_i2c_atmel_data_read(TPMState *s)
{
    TPMTISEmuState *tis = &s->s.tis;
    uint32_t ret = TPM_TIS_NO_DATA_BYTE;
    uint16_t len;

    if ((tis->loc[0].sts & TPM_TIS_STS_DATA_AVAILABLE)) {
        len = tpm_i2c_atmel_get_size_from_buffer(&tis->loc[0].r_buffer);

        ret = tis->loc[0].r_buffer.buffer[tis->loc[0].r_offset++];
        if (tis->loc[0].r_offset >= len) {
            /* got last byte */
            tpm_i2c_atmel_sts_set(&tis->loc[0], TPM_TIS_STS_VALID);
        }
        DPRINTF("tpm_i2c_atmel: tpm_i2c_atmel_data_read byte 0x%02x   [%d]\n",
                ret, tis->loc[0].r_offset-1);
    } else {
        DPRINTF("tpm_i2c_atmel: !TPM_TIS_STS_DATA_AVAILABLE [%d]\n",
                tis->loc[0].sts);
    }

    return ret;
}

static void tpm_i2c_atmel_event(I2CSlave *i2c, enum i2c_event event)
{
    TPMState *s = TPM(&(i2c->qdev));
    i2c->busy = 0;

    switch (event) {
    case I2C_START_RECV:
        i2c->busy = tpm_i2c_atmel_tpm_start_recv(s);
        break;
    case I2C_START_SEND:
        tpm_i2c_atmel_tpm_start_send(s);
        break;
    case I2C_FINISH:
        tpm_i2c_atmel_tpm_send(s);
        break;
    default:
        break;
    }
}

static int tpm_i2c_atmel_recv(I2CSlave *i2c)
{
    TPMState *s = TPM(&(i2c->qdev));
    return tpm_i2c_atmel_data_read(s);
}

static int tpm_i2c_atmel_send(I2CSlave *i2c, uint8_t data)
{
    TPMState *s = TPM(&(i2c->qdev));
    TPMTISEmuState *tis = &s->s.tis;
    tis->loc[0].w_buffer.buffer[tis->loc[0].w_offset++] = data;
    return 0;
}

static void tpm_i2c_atmel_receive_cb(TPMState *s, uint8_t locty,
                               bool is_selftest_done)
{
    TPMTISEmuState *tis = &s->s.tis;

    assert(locty == 0);

    if (is_selftest_done) {
        tis->loc[0].sts |= TPM_TIS_STS_SELFTEST_DONE;
    }

    qemu_bh_schedule(tis->bh);
}


static void tpm_i2c_atmel_realizefn(DeviceState *dev, Error **errp)
{
    TPMState *s = TPM(dev);
    TPMTISEmuState *tis = &s->s.tis;

    DPRINTF("backend %s\n", s->backend);
    s->be_driver = qemu_find_tpm(s->backend);
    if (!s->be_driver) {
        error_setg(errp, "tpm_i2c_atmel: backend driver with id %s could not be "
                   "found", s->backend);
        return;
    }

    s->be_driver->fe_model = TPM_MODEL_TPM_TIS;

    if (tpm_backend_init(s->be_driver, s, tpm_i2c_atmel_receive_cb)) {
        error_setg(errp, "tpm_i2c_atmel: backend driver with id %s could not be "
                   "initialized", s->backend);
        return;
    }

    if (tis->irq_num > 15) {
        error_setg(errp, "tpm_i2c_atmel: IRQ %d for TPM TIS is outside valid range "
                   "of 0 to 15", tis->irq_num);
        return;
    }

    tis->bh = qemu_bh_new(tpm_i2c_atmel_receive_bh, s);
}

static int tpm_i2c_atmel_do_startup_tpm(TPMState *s)
{
    return tpm_backend_startup_tpm(s->be_driver);
}


static int tpm_i2c_atmel_init(I2CSlave *i2c)
{
    return 0;
}

static void tpm_i2c_atmel_reset(DeviceState *dev)
{
    TPMState *s = TPM(dev);
    TPMTISEmuState *tis = &s->s.tis;

    s->be_tpm_version = tpm_backend_get_tpm_version(s->be_driver);

    tpm_backend_reset(s->be_driver);

    tis->active_locty = TPM_TIS_NO_LOCALITY;
    tis->next_locty = TPM_TIS_NO_LOCALITY;
    tis->aborting_locty = TPM_TIS_NO_LOCALITY;

    /* ATMEL AT97SC3204T only uses locality 0 */
    memset(tis->loc, 0, sizeof(tis->loc));
    tis->loc[0].access = TPM_TIS_ACCESS_TPM_REG_VALID_STS;
    switch (s->be_tpm_version) {
    case TPM_VERSION_UNSPEC:
        break;
    case TPM_VERSION_1_2:
        tis->loc[0].sts = TPM_TIS_STS_TPM_FAMILY1_2;
        tis->loc[0].iface_id = TPM_TIS_IFACE_ID_SUPPORTED_FLAGS1_3;
        break;
    case TPM_VERSION_2_0:
        tis->loc[0].sts = TPM_TIS_STS_TPM_FAMILY2_0;
        tis->loc[0].iface_id = TPM_TIS_IFACE_ID_SUPPORTED_FLAGS2_0;
        break;
    }
        tis->loc[0].state = TPM_TIS_STATE_IDLE;

    tpm_backend_realloc_buffer(s->be_driver, &tis->loc[0].w_buffer);
    tpm_backend_realloc_buffer(s->be_driver, &tis->loc[0].r_buffer);

    tpm_i2c_atmel_do_startup_tpm(s);
}

static Property tpm_tis_properties[] = {
    DEFINE_PROP_UINT32("irq", TPMState,
                       s.tis.irq_num, TPM_TIS_IRQ),
    DEFINE_PROP_STRING("tpmdev", TPMState, backend),
    DEFINE_PROP_END_OF_LIST(),
};

static void tpm_i2c_atmel_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    I2CSlaveClass *k = I2C_SLAVE_CLASS(klass);

    k->init = tpm_i2c_atmel_init;
    k->event = tpm_i2c_atmel_event;
    k->recv = tpm_i2c_atmel_recv;
    k->send = tpm_i2c_atmel_send;
    dc->realize = tpm_i2c_atmel_realizefn;
    dc->props = tpm_tis_properties;
    dc->reset = tpm_i2c_atmel_reset;
    dc->vmsd = &vmstate_tpm_i2c_atmel;
}

static const TypeInfo tpm_i2c_atmel_info = {
    .name          = TYPE_TPM_TIS,
    .parent        = TYPE_I2C_SLAVE,
    .instance_size = sizeof(TPMState),
    .class_init    = tpm_i2c_atmel_class_init,
};

static void tpm_i2c_atmel_register_types(void)
{
    type_register_static(&tpm_i2c_atmel_info);
    tpm_register_model(TPM_MODEL_TPM_TIS);
}

type_init(tpm_i2c_atmel_register_types)
