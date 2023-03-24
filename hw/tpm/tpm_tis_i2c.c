/*
 * tpm_tis_i2c.c - QEMU's TPM TIS I2C Device
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 *
 * Implementation of the TIS interface according to specs found at
 * http://www.trustedcomputinggroup.org. This implementation currently
 * supports version 1.3, 21 March 2013
 * In the developers menu choose the PC Client section then find the TIS
 * specification.
 *
 * TPM TIS for TPM 2 implementation following TCG PC Client Platform
 * TPM Profile (PTP) Specification, Familiy 2.0, Revision 00.43
 *
 * TPM I2C implementation follows TCG TPM I2c Interface specification,
 * Family 2.0, Level 00, Revision 1.00
 */

#include "qemu/osdep.h"
#include "hw/i2c/i2c.h"
#include "hw/qdev-properties.h"
#include "hw/acpi/tpm.h"
#include "migration/vmstate.h"
#include "tpm_prop.h"
#include "tpm_tis.h"
#include "qom/object.h"
#include "block/aio.h"
#include "qemu/main-loop.h"
#include "qemu/log.h"

/* TPM_STS mask for read bits 31:26 must be zero */
#define TPM_I2C_STS_READ_MASK          0x03ffffff

/* Operations */
#define OP_SEND   1
#define OP_RECV   2

typedef struct TPMStateI2C {
    /*< private >*/
    I2CSlave parent_obj;

    int      offset;     /* offset in to data[] */
    int      size;       /* Size of the current reg data */
    uint8_t  operation;  /* OP_SEND & OP_RECV */
    uint8_t  data[16];   /* Data */

    uint8_t  locality;      /* Current locality */

    bool     checksum_enable;
    uint32_t tis_intf_cap;  /* save TIS interface Capabilities */

    /*< public >*/
    TPMState state; /* not a QOM object */

} TPMStateI2C;

DECLARE_INSTANCE_CHECKER(TPMStateI2C, TPM_TIS_I2C,
                         TYPE_TPM_TIS_I2C)

/* Register map */
typedef struct regMap {
    uint16_t  i2c_reg;    /* I2C register */
    uint16_t  tis_reg;    /* TIS register */
    uint32_t  data_size;  /* data size expected */
} i2cRegMap;

/*
 * The register values in the common code is different than the latest
 * register numbers as per the spec hence add the conversion map
 */
static const i2cRegMap tpm_tis_reg_map[] = {
    /* These registers are sent to TIS layer */
    { TPM_TIS_I2C_REG_ACCESS,           TPM_TIS_REG_ACCESS,               1, },
    { TPM_TIS_I2C_REG_INT_ENABLE,       TPM_TIS_REG_INT_ENABLE,           4, },
    { TPM_TIS_I2C_REG_INT_CAPABILITY,   TPM_TIS_REG_INT_VECTOR,           4, },
    { TPM_TIS_I2C_REG_STS,              TPM_TIS_REG_STS,                  4, },
    { TPM_TIS_I2C_REG_DATA_FIFO,        TPM_TIS_REG_DATA_FIFO,            0, },
    { TPM_TIS_I2C_REG_INTF_CAPABILITY,  TPM_TIS_REG_INTF_CAPABILITY,      4, },
    { TPM_TIS_I2C_REG_DID_VID,          TPM_TIS_REG_DID_VID,              4, },
    { TPM_TIS_I2C_REG_RID,              TPM_TIS_REG_RID,                  1, },

    /* These registers are handled in I2C layer */
    { TPM_TIS_I2C_REG_LOC_SEL,          TPM_TIS_I2C_REG_LOC_SEL,          1, },
    { TPM_TIS_I2C_REG_I2C_DEV_ADDRESS,  TPM_TIS_I2C_REG_I2C_DEV_ADDRESS,  2, },
    { TPM_TIS_I2C_REG_DATA_CSUM_ENABLE, TPM_TIS_I2C_REG_DATA_CSUM_ENABLE, 1, },
    { TPM_TIS_I2C_REG_DATA_CSUM_GET,    TPM_TIS_I2C_REG_DATA_CSUM_GET,    2, },
};

/*
 * Generate interface capability based on what is returned by TIS and what is
 * expected by I2C. Save the capability in the data array overwriting the TIS
 * capability.
 */
static uint32_t tpm_i2c_interface_capability(TPMStateI2C *i2cst,
                                             uint32_t tis_cap)
{
    uint32_t i2c_cap = 0;

    i2cst->tis_intf_cap = tis_cap;

    /* Now generate i2c capability */
    i2c_cap = (TPM_I2C_CAP_INTERFACE_TYPE |
               TPM_I2C_CAP_INTERFACE_VER  |
               TPM_I2C_CAP_TPM2_FAMILY    |
               TPM_I2C_CAP_LOCALITY_CAP   |
               TPM_I2C_CAP_BUS_SPEED      |
               TPM_I2C_CAP_DEV_ADDR_CHANGE);

    /* Now check the TIS and set some capabilities */

    /* Static burst count set */
    if (i2cst->tis_intf_cap & TPM_TIS_CAP_BURST_COUNT_STATIC) {
        i2c_cap |= TPM_I2C_CAP_BURST_COUNT_STATIC;
    }

    return i2c_cap;
}

static inline uint16_t tpm_tis_i2c_to_tis_reg(TPMStateI2C *i2cst, int *size)
{
    uint16_t tis_reg = 0xffff;
    const i2cRegMap *reg_map;
    int i;

    for (i = 0; i < ARRAY_SIZE(tpm_tis_reg_map); i++) {
        reg_map = &tpm_tis_reg_map[i];
        if (reg_map->i2c_reg == (i2cst->data[0] & 0xff)) {
            tis_reg = reg_map->tis_reg;
            *size = reg_map->data_size;
            break;
        }
    }

    if (tis_reg == 0xffff) {
        qemu_log_mask(LOG_UNIMP, "%s: Could not convert i2c register: 0x%X\n",
                      __func__, i2cst->data[0]);
    }

    /* Include the locality in the address. */
    if (i2cst->locality != TPM_TIS_NO_LOCALITY) {
        tis_reg += (i2cst->locality << TPM_TIS_LOCALITY_SHIFT);
    }

    return tis_reg;
}

/* Clear some fields from the structure. */
static inline void tpm_tis_i2c_clear_data(TPMStateI2C *i2cst)
{
    /* Clear operation and offset */
    i2cst->operation = 0;
    i2cst->offset = 0;
    i2cst->size = 0;

    return;
}

/* Find endianness */
static inline bool tpm_i2c_is_little_endian(void)
{
    uint32_t val = 1;
    char     *ch = (char *)&val;

    return (uint32_t)*ch;
}

/*
 * Convert uint32 to stream of bytes in little endian format.
 */
static inline void tpm_i2c_uint_to_le_bytes(TPMStateI2C *i2cst, uint32_t data)
{
    int      i;

    /* Index 0 is register so do not thouch it. */
    if (tpm_i2c_is_little_endian()) {
        for (i = 1; i <= 4; i++) {
            i2cst->data[i] = (data & 0xff);
            data >>= 8;
        }
    } else {
        for (i = 4; i > 0; i--) {
            i2cst->data[i] = (data & 0xff);
            data >>= 8;
        }
    }
}

/*
 * Convert little endian byte stream into local formated
 * unsigned integer
 */
static inline uint32_t tpm_i2c_le_bytes_to_uint(TPMStateI2C *i2cst)
{
    uint32_t data = 0;
    int      i;

    assert(i2cst->offset <= 5);  /* Including 0th register value */

    if (tpm_i2c_is_little_endian()) {
        for (i = 1; i < i2cst->offset; i++) {
            data |= (((uint32_t)i2cst->data[i]) << (8 * (i - 1)));
        }
    } else {
        for (i = 1; i < i2cst->offset; i++) {
            data <<= 8;
            data |= i2cst->data[i];
        }
    }

    return data;
}

/* Send data to TPM */
static inline void tpm_tis_i2c_tpm_send(TPMStateI2C *i2cst)
{
    uint16_t tis_reg;
    uint32_t data;

    if ((i2cst->operation == OP_SEND) && (i2cst->offset > 1)) {

        switch (i2cst->data[0]) {
        case TPM_TIS_I2C_REG_DATA_CSUM_ENABLE:
            /*
             * Checksum is not handled by TIS code hence we will consume the
             * register here.
             */
            i2cst->checksum_enable = true;
            break;
        case TPM_TIS_I2C_REG_DATA_FIFO:
            /* Handled in the main i2c_send function */
            break;
        case TPM_TIS_I2C_REG_LOC_SEL:
            /*
             * This register is not handled by TIS so save the locality
             * locally
             */
            i2cst->locality = i2cst->data[1];
            break;
        default:
            /* We handle non-FIFO here */
            tis_reg = tpm_tis_i2c_to_tis_reg(i2cst, &i2cst->size);

            /* Index 0 is always a register. Convert string to uint32_t. */
            data = tpm_i2c_le_bytes_to_uint(i2cst);

            tpm_tis_write_data(&i2cst->state, tis_reg, data, 4);
            break;
        }

        tpm_tis_i2c_clear_data(i2cst);
    }

    return;
}

/* Callback from TPM to indicate that response is copied */
static void tpm_tis_i2c_request_completed(TPMIf *ti, int ret)
{
    TPMStateI2C *i2cst = TPM_TIS_I2C(ti);
    TPMState *s = &i2cst->state;

    /* Inform the common code. */
    tpm_tis_request_completed(s, ret);
}

static enum TPMVersion tpm_tis_i2c_get_tpm_version(TPMIf *ti)
{
    TPMStateI2C *i2cst = TPM_TIS_I2C(ti);
    TPMState *s = &i2cst->state;

    return tpm_tis_get_tpm_version(s);
}

static int tpm_tis_i2c_event(I2CSlave *i2c, enum i2c_event event)
{
    TPMStateI2C *i2cst = TPM_TIS_I2C(i2c);
    int ret = 0;

    switch (event) {
    case I2C_START_RECV:
        break;
    case I2C_START_SEND:
        tpm_tis_i2c_clear_data(i2cst);
        break;
    case I2C_FINISH:
        if (i2cst->operation == OP_SEND) {
            tpm_tis_i2c_tpm_send(i2cst);
        } else {
            tpm_tis_i2c_clear_data(i2cst);
        }
        break;
    default:
        break;
    }

    return ret;
}

/*
 * If data is for FIFO then it is received from tpm_tis_common buffer
 * otherwise it will be handled using single call to common code and
 * cached in the local buffer.
 */
static uint8_t tpm_tis_i2c_recv(I2CSlave *i2c)
{
    int          ret = 0;
    uint32_t     addr;
    uint32_t     data_read;
    TPMStateI2C *i2cst = TPM_TIS_I2C(i2c);
    TPMState    *s = &i2cst->state;
    uint16_t     i2c_reg = i2cst->data[0];

    /* Convert I2C register to TIS register */
    addr = tpm_tis_i2c_to_tis_reg(i2cst, &i2cst->size);
    if (addr == 0xffff) {
        return 0;
    }

    if (i2cst->operation == OP_RECV) {

        /* Do not cache FIFO data. */
        if (i2cst->data[0] == TPM_TIS_I2C_REG_DATA_FIFO) {
            data_read = tpm_tis_read_data(s, addr, 1);
            ret = (data_read & 0xff);
        } else if (sizeof(i2cst->data)) {
            ret = i2cst->data[i2cst->offset++];
        }

    } else if ((i2cst->operation == OP_SEND) && (i2cst->offset < 2)) {
        /* First receive call after send */

        i2cst->operation = OP_RECV;

        switch (i2c_reg) {
        case TPM_TIS_I2C_REG_LOC_SEL:
            /* Location selection register is managed by i2c */
            i2cst->data[1] = i2cst->locality;
            break;
        case TPM_TIS_I2C_REG_DATA_FIFO:
            /* FIFO data is directly read from TPM TIS */
            data_read = tpm_tis_read_data(s, addr, 1);
            i2cst->data[1] = (data_read & 0xff);
            break;
        case TPM_TIS_I2C_REG_DATA_CSUM_ENABLE:
            i2cst->data[1] = i2cst->checksum_enable;
            break;
        case TPM_TIS_I2C_REG_DATA_CSUM_GET:
            /*
             * Checksum registers are not supported by common code hence
             * call a common code to get the checksum.
             */
            data_read = tpm_tis_get_checksum(s, i2cst->locality);
            /*
             * Save the data in little endian byte stream in the data
             * field.
             */
            tpm_i2c_uint_to_le_bytes(i2cst, data_read);
            break;
        default:
            data_read = tpm_tis_read_data(s, addr, 4);

            if (i2c_reg == TPM_TIS_I2C_REG_INTF_CAPABILITY) {
                /* Prepare the capabilities as per I2C interface */
                data_read = tpm_i2c_interface_capability(i2cst,
                                                         data_read);
            } else if (i2c_reg == TPM_TIS_I2C_REG_STS) {
                /*
                 * As per specs, STS bit 31:26 are reserved and must
                 * be set to 0
                 */
                data_read &= TPM_I2C_STS_READ_MASK;
            }
            /*
             * Save the data in little endian byte stream in the data
             * field.
             */
            tpm_i2c_uint_to_le_bytes(i2cst, data_read);
            break;
        }


        /* Return first byte with this call */
        i2cst->offset = 1; /* keep the register value intact for debug */
        ret = i2cst->data[i2cst->offset++];
    } else {
        i2cst->operation = OP_RECV;
    }

    return ret;
}

/*
 * Send function only remembers data in the buffer and then calls
 * TPM TIS common code during FINISH event.
 */
static int tpm_tis_i2c_send(I2CSlave *i2c, uint8_t data)
{
    TPMStateI2C *i2cst = TPM_TIS_I2C(i2c);
    uint16_t tis_reg;

    /* Reject non-supported registers. */
    if (i2cst->offset == 0) {
        /* We do not support device address change */
        if (data == TPM_TIS_I2C_REG_I2C_DEV_ADDRESS) {
            qemu_log_mask(LOG_UNIMP, "%s: Device address change "
                          "is not supported.\n", __func__);
            return 1;
        }
    }

    if (sizeof(i2cst->data)) {
        i2cst->operation = OP_SEND;

        /* Remember data locally for non-FIFO registers */
        if ((i2cst->offset == 0) ||
            (i2cst->data[0] != TPM_TIS_I2C_REG_DATA_FIFO)) {
            i2cst->data[i2cst->offset++] = data;
        } else {
            tis_reg = tpm_tis_i2c_to_tis_reg(i2cst, &i2cst->size);
            tpm_tis_write_data(&i2cst->state, tis_reg, data, 1);
        }

        return 0;

    } else {
        /* Return non-zero to indicate NAK */
        return 1;
    }
}

static Property tpm_tis_i2c_properties[] = {
    DEFINE_PROP_UINT32("irq", TPMStateI2C, state.irq_num, TPM_TIS_IRQ),
    DEFINE_PROP_TPMBE("tpmdev", TPMStateI2C, state.be_driver),
    DEFINE_PROP_END_OF_LIST(),
};

static void tpm_tis_i2c_realizefn(DeviceState *dev, Error **errp)
{
    TPMStateI2C *i2cst = TPM_TIS_I2C(dev);
    TPMState *s = &i2cst->state;

    if (!tpm_find()) {
        error_setg(errp, "at most one TPM device is permitted");
        return;
    }

    /*
     * Get the backend pointer. It is not initialized propery during
     * device_class_set_props
     */
    s->be_driver = qemu_find_tpm_be("tpm0");

    if (!s->be_driver) {
        error_setg(errp, "'tpmdev' property is required");
        return;
    }
    if (s->irq_num > 15) {
        error_setg(errp, "IRQ %d is outside valid range of 0 to 15",
                   s->irq_num);
        return;
    }
}

static void tpm_tis_i2c_reset(DeviceState *dev)
{
    TPMStateI2C *i2cst = TPM_TIS_I2C(dev);
    TPMState *s = &i2cst->state;

    tpm_tis_i2c_clear_data(i2cst);

    i2cst->checksum_enable = false;
    i2cst->locality = TPM_TIS_NO_LOCALITY;

    return tpm_tis_reset(s);
}

static void tpm_tis_i2c_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    I2CSlaveClass *k = I2C_SLAVE_CLASS(klass);
    TPMIfClass *tc = TPM_IF_CLASS(klass);

    dc->realize = tpm_tis_i2c_realizefn;
    dc->reset = tpm_tis_i2c_reset;
    device_class_set_props(dc, tpm_tis_i2c_properties);

    k->event = tpm_tis_i2c_event;
    k->recv = tpm_tis_i2c_recv;
    k->send = tpm_tis_i2c_send;

    tc->model = TPM_MODEL_TPM_TIS;
    tc->request_completed = tpm_tis_i2c_request_completed;
    tc->get_version = tpm_tis_i2c_get_tpm_version;
}

static const TypeInfo tpm_tis_i2c_info = {
    .name          = TYPE_TPM_TIS_I2C,
    .parent        = TYPE_I2C_SLAVE,
    .instance_size = sizeof(TPMStateI2C),
    .class_init    = tpm_tis_i2c_class_init,
        .interfaces = (InterfaceInfo[]) {
        { TYPE_TPM_IF },
        { }
    }
};

static void tpm_tis_i2c_register_types(void)
{
    type_register_static(&tpm_tis_i2c_info);
}

type_init(tpm_tis_i2c_register_types)
