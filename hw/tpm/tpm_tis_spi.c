#include "qemu/osdep.h"
#include "hw/qdev-properties.h"
#include "migration/vmstate.h"
#include "hw/acpi/tpm.h"
#include "tpm_prop.h"
#include "tpm_tis.h"
#include "qom/object.h"
#include "hw/ssi/ssi.h"
#include "hw/ssi/spi_gpio.h"

#define TPM_TIS_SPI_ADDR_BYTES 3
#define SPI_WRITE 0

typedef enum {
    TIS_SPI_PKT_STATE_DEACTIVATED = 0,
    TIS_SPI_PKT_STATE_START,
    TIS_SPI_PKT_STATE_ADDRESS,
    TIS_SPI_PKT_STATE_DATA_WR,
    TIS_SPI_PKT_STATE_DATA_RD,
    TIS_SPI_PKT_STATE_DONE,
} TpmTisSpiPktState;

union TpmTisRWSizeByte {
    uint8_t byte;
    struct {
        uint8_t data_expected_size:6;
        uint8_t resv:1;
        uint8_t rwflag:1;
    };
};

union TpmTisSpiHwAddr {
    hwaddr addr;
    uint8_t bytes[sizeof(hwaddr)];
};

union TpmTisSpiData {
    uint32_t data;
    uint8_t bytes[64];
};

struct TpmTisSpiState {
    /*< private >*/
    SSIPeripheral parent_obj;

    /*< public >*/
    TPMState tpm_state; /* not a QOM object */
    TpmTisSpiPktState tpm_tis_spi_state;

    union TpmTisRWSizeByte first_byte;
    union TpmTisSpiHwAddr addr;
    union TpmTisSpiData data;

    uint32_t data_size;
    uint8_t data_idx;
    uint8_t addr_idx;
};

struct TpmTisSpiClass {
    SSIPeripheralClass parent_class;
};

OBJECT_DECLARE_TYPE(TpmTisSpiState, TpmTisSpiClass, TPM_TIS_SPI)

static void tpm_tis_spi_mmio_read(TpmTisSpiState *tts)
{
    uint16_t offset = tts->addr.addr & 0xffc;

    switch (offset) {
    case TPM_TIS_REG_DATA_FIFO:
        for (uint8_t i = 0; i < tts->data_size; i++) {
            tts->data.bytes[i] = (uint8_t)tpm_tis_memory_ops.read(
                                          &tts->tpm_state,
                                          tts->addr.addr,
                                          1);
        }
        break;
    default:
        tts->data.data = (uint32_t)tpm_tis_memory_ops.read(
                                   &tts->tpm_state,
                                   tts->addr.addr,
                                   tts->data_size);
    }
}

static void tpm_tis_spi_mmio_write(TpmTisSpiState *tts)
{
    uint16_t offset = tts->addr.addr & 0xffc;

    switch (offset) {
    case TPM_TIS_REG_DATA_FIFO:
        for (uint8_t i = 0; i < tts->data_size; i++) {
            tpm_tis_memory_ops.write(&tts->tpm_state,
                                     tts->addr.addr,
                                     tts->data.bytes[i],
                                     1);
        }
        break;
    default:
        tpm_tis_memory_ops.write(&tts->tpm_state,
                                 tts->addr.addr,
                                 tts->data.data,
                                 tts->data_size);
        }
}

static uint32_t tpm_tis_spi_transfer8(SSIPeripheral *ss, uint32_t tx)
{
    TpmTisSpiState *tts = TPM_TIS_SPI(ss);
    uint32_t r = 1;

    switch (tts->tpm_tis_spi_state) {
    case TIS_SPI_PKT_STATE_START:
        tts->first_byte.byte = (uint8_t)tx;
        tts->data_size = tts->first_byte.data_expected_size + 1;
        tts->data_idx = 0;
        tts->addr_idx = TPM_TIS_SPI_ADDR_BYTES;
        tts->tpm_tis_spi_state = TIS_SPI_PKT_STATE_ADDRESS;
        break;
    case TIS_SPI_PKT_STATE_ADDRESS:
        assert(tts->addr_idx > 0);
        tts->addr.bytes[--tts->addr_idx] = (uint8_t)tx;

        if (tts->addr_idx == 0) {
            if (tts->first_byte.rwflag == SPI_WRITE) {
                tts->tpm_tis_spi_state = TIS_SPI_PKT_STATE_DATA_WR;
            } else { /* read and get the data ready */
                tpm_tis_spi_mmio_read(tts);
                tts->tpm_tis_spi_state = TIS_SPI_PKT_STATE_DATA_RD;
            }
        }
        break;
    case TIS_SPI_PKT_STATE_DATA_WR:
        tts->data.bytes[tts->data_idx++] = (uint8_t)tx;
        if (tts->data_idx == tts->data_size) {
            tpm_tis_spi_mmio_write(tts);
            tts->tpm_tis_spi_state = TIS_SPI_PKT_STATE_DONE;
        }
        break;
    case TIS_SPI_PKT_STATE_DATA_RD:
        r = tts->data.bytes[tts->data_idx++];
        if (tts->data_idx == tts->data_size) {
            tts->tpm_tis_spi_state = TIS_SPI_PKT_STATE_DONE;
        }
        break;
    default:
        tts->tpm_tis_spi_state = TIS_SPI_PKT_STATE_DEACTIVATED;
        r = (uint32_t) -1;
    }

    return r;
}

/*
 * Pre-reading logic for transfer:
 * This is to fix the transaction between reading and writing.
 * The first byte is arbitrarily inserted so we need to
 * shift the all the output bytes (timeline) one byte right.
 */
static uint32_t tpm_tis_spi_transfer8_ex(SSIPeripheral *ss, uint32_t tx)
{
    TpmTisSpiState *tts = TPM_TIS_SPI(ss);
    SSIBus *ssibus = (SSIBus *)qdev_get_parent_bus(DEVICE(tts));

    TpmTisSpiPktState prev_state = tts->tpm_tis_spi_state;
    uint32_t r = tpm_tis_spi_transfer8(ss, tx);
    TpmTisSpiPktState curr_state = tts->tpm_tis_spi_state;

    if (ssibus->preread &&
       /* cmd state has changed into DATA reading state */
       prev_state != TIS_SPI_PKT_STATE_DATA_RD &&
       curr_state == TIS_SPI_PKT_STATE_DATA_RD) {
        r = tpm_tis_spi_transfer8(ss, 0xFF);
    }

    return r;
}

static int tpm_tis_spi_cs(SSIPeripheral *ss, bool select)
{
    TpmTisSpiState *tts = TPM_TIS_SPI(ss);

    if (select) { /* cs de-assert */
        tts->tpm_tis_spi_state = TIS_SPI_PKT_STATE_DEACTIVATED;
    } else {
        tts->tpm_tis_spi_state = TIS_SPI_PKT_STATE_START;
        tts->first_byte.byte = 0;
        tts->addr.addr = 0;
        tts->data.data = 0;
    }

    return 0;
}

static int tpm_tis_pre_save_spi(void *opaque)
{
    TpmTisSpiState *sbdev = opaque;

    return tpm_tis_pre_save(&sbdev->tpm_state);
}

static const VMStateDescription vmstate_tpm_tis_spi = {
    .name = "tpm-tis-spi",
    .version_id = 0,
    .pre_save  = tpm_tis_pre_save_spi,
    .fields = (VMStateField[]) {
        VMSTATE_BUFFER(tpm_state.buffer, TpmTisSpiState),
        VMSTATE_UINT16(tpm_state.rw_offset, TpmTisSpiState),
        VMSTATE_UINT8(tpm_state.active_locty, TpmTisSpiState),
        VMSTATE_UINT8(tpm_state.aborting_locty, TpmTisSpiState),
        VMSTATE_UINT8(tpm_state.next_locty, TpmTisSpiState),

        VMSTATE_STRUCT_ARRAY(tpm_state.loc, TpmTisSpiState, TPM_TIS_NUM_LOCALITIES,
                             0, vmstate_locty, TPMLocality),

        VMSTATE_END_OF_LIST()
    }
};

static void tpm_tis_spi_request_completed(TPMIf *ti, int ret)
{
    TpmTisSpiState *sbdev = TPM_TIS_SPI(ti);
    TPMState *s = &sbdev->tpm_state;

    tpm_tis_request_completed(s, ret);
}

static enum TPMVersion tpm_tis_spi_get_tpm_version(TPMIf *ti)
{
    TpmTisSpiState *sbdev = TPM_TIS_SPI(ti);
    TPMState *s = &sbdev->tpm_state;

    return tpm_tis_get_tpm_version(s);
}

static void tpm_tis_spi_reset(DeviceState *dev)
{
    TpmTisSpiState *sbdev = TPM_TIS_SPI(dev);
    TPMState *s = &sbdev->tpm_state;

    return tpm_tis_reset(s);
}

static Property tpm_tis_spi_properties[] = {
    DEFINE_PROP_UINT32("irq", TpmTisSpiState, tpm_state.irq_num, TPM_TIS_IRQ),
    DEFINE_PROP_TPMBE("tpmdev", TpmTisSpiState, tpm_state.be_driver),
    DEFINE_PROP_BOOL("ppi", TpmTisSpiState, tpm_state.ppi_enabled, false),
    DEFINE_PROP_END_OF_LIST(),
};

static void tpm_tis_spi_realizefn(SSIPeripheral *ss, Error **errp)
{
    TpmTisSpiState *sbdev = TPM_TIS_SPI(ss);

    if (!tpm_find()) {
        error_setg(errp, "at most one TPM device is permitted");
        return;
    }

    if (!sbdev->tpm_state.be_driver) {
        error_setg(errp, "'tpmdev' property is required");
        return;
    }

    DeviceState *spi_gpio = qdev_find_recursive(sysbus_get_default(),
                                                TYPE_SPI_GPIO);
    qdev_connect_gpio_out_named(spi_gpio,
                                "SPI_CS_out", 0,
                                qdev_get_gpio_in_named(DEVICE(ss),
                                SSI_GPIO_CS, 0));
}

static void tpm_tis_spi_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    SSIPeripheralClass *k = SSI_PERIPHERAL_CLASS(klass);
    TPMIfClass *tc = TPM_IF_CLASS(klass);

    device_class_set_props(dc, tpm_tis_spi_properties);
    k->realize = tpm_tis_spi_realizefn;
    k->transfer = tpm_tis_spi_transfer8_ex;
    k->set_cs = tpm_tis_spi_cs;
    k->cs_polarity = SSI_CS_LOW;

    dc->vmsd  = &vmstate_tpm_tis_spi;
    tc->model = TPM_MODEL_TPM_TIS;
    dc->user_creatable = true;
    dc->reset = tpm_tis_spi_reset;
    tc->request_completed = tpm_tis_spi_request_completed;
    tc->get_version = tpm_tis_spi_get_tpm_version;
    set_bit(DEVICE_CATEGORY_MISC, dc->categories);
}

static const TypeInfo tpm_tis_spi_info = {
    .name = TYPE_TPM_TIS_SPI,
    .parent = TYPE_SSI_PERIPHERAL,
    .instance_size = sizeof(TpmTisSpiState),
    .class_size = sizeof(TpmTisSpiClass),
    .class_init  = tpm_tis_spi_class_init,
    .interfaces = (InterfaceInfo[]) {
        { TYPE_TPM_IF },
        { }
    }
};

static void tpm_tis_spi_register(void)
{
    type_register_static(&tpm_tis_spi_info);
}

type_init(tpm_tis_spi_register)
