/*
 * PXA270-based Zipit Z2 device
 *
 * Copyright (c) 2011 by Vasily Khoruzhick <anarsoul@gmail.com>
 *
 * Code is based on mainstone platform.
 *
 * This code is licensed under the GNU GPL v2.
 *
 * Contributions after 2012-01-13 are licensed under the terms of the
 * GNU GPL, version 2 or (at your option) any later version.
 */

#include "qemu/osdep.h"
#include "qemu/units.h"
#include "hw/arm/pxa.h"
#include "hw/arm/boot.h"
#include "hw/i2c/i2c.h"
#include "hw/irq.h"
#include "hw/ssi/ssi.h"
#include "migration/vmstate.h"
#include "hw/boards.h"
#include "hw/block/flash.h"
#include "ui/console.h"
#include "hw/audio/wm8750.h"
#include "audio/audio.h"
#include "exec/address-spaces.h"
#include "qom/object.h"
#include "qapi/error.h"
#include "trace.h"

static const struct keymap map[0x100] = {
    [0 ... 0xff] = { -1, -1 },
    [0x3b] = {0, 0}, /* Option = F1 */
    [0xc8] = {0, 1}, /* Up */
    [0xd0] = {0, 2}, /* Down */
    [0xcb] = {0, 3}, /* Left */
    [0xcd] = {0, 4}, /* Right */
    [0xcf] = {0, 5}, /* End */
    [0x0d] = {0, 6}, /* KPPLUS */
    [0xc7] = {1, 0}, /* Home */
    [0x10] = {1, 1}, /* Q */
    [0x17] = {1, 2}, /* I */
    [0x22] = {1, 3}, /* G */
    [0x2d] = {1, 4}, /* X */
    [0x1c] = {1, 5}, /* Enter */
    [0x0c] = {1, 6}, /* KPMINUS */
    [0xc9] = {2, 0}, /* PageUp */
    [0x11] = {2, 1}, /* W */
    [0x18] = {2, 2}, /* O */
    [0x23] = {2, 3}, /* H */
    [0x2e] = {2, 4}, /* C */
    [0x38] = {2, 5}, /* LeftAlt */
    [0xd1] = {3, 0}, /* PageDown */
    [0x12] = {3, 1}, /* E */
    [0x19] = {3, 2}, /* P */
    [0x24] = {3, 3}, /* J */
    [0x2f] = {3, 4}, /* V */
    [0x2a] = {3, 5}, /* LeftShift */
    [0x01] = {4, 0}, /* Esc */
    [0x13] = {4, 1}, /* R */
    [0x1e] = {4, 2}, /* A */
    [0x25] = {4, 3}, /* K */
    [0x30] = {4, 4}, /* B */
    [0x1d] = {4, 5}, /* LeftCtrl */
    [0x0f] = {5, 0}, /* Tab */
    [0x14] = {5, 1}, /* T */
    [0x1f] = {5, 2}, /* S */
    [0x26] = {5, 3}, /* L */
    [0x31] = {5, 4}, /* N */
    [0x39] = {5, 5}, /* Space */
    [0x3c] = {6, 0}, /* Stop = F2 */
    [0x15] = {6, 1}, /* Y */
    [0x20] = {6, 2}, /* D */
    [0x0e] = {6, 3}, /* Backspace */
    [0x32] = {6, 4}, /* M */
    [0x33] = {6, 5}, /* Comma */
    [0x3d] = {7, 0}, /* Play = F3 */
    [0x16] = {7, 1}, /* U */
    [0x21] = {7, 2}, /* F */
    [0x2c] = {7, 3}, /* Z */
    [0x27] = {7, 4}, /* Semicolon */
    [0x34] = {7, 5}, /* Dot */
};

#define Z2_RAM_SIZE     0x02000000
#define Z2_FLASH_BASE   0x00000000
#define Z2_FLASH_SIZE   0x00800000

static struct arm_boot_info z2_binfo = {
    .loader_start   = PXA2XX_SDRAM_BASE,
    .ram_size       = Z2_RAM_SIZE,
};

#define Z2_GPIO_SD_DETECT   96
#define Z2_GPIO_AC_IN       0
#define Z2_GPIO_KEY_ON      1
#define Z2_GPIO_LCD_CS      88

struct ZipitLCD {
    SSIPeripheral ssidev;
    int32_t selected;
    int32_t enabled;
    uint8_t buf[3];
    uint32_t cur_reg;
    int pos;
};

#define TYPE_ZIPIT_LCD "zipit-lcd"
OBJECT_DECLARE_SIMPLE_TYPE(ZipitLCD, ZIPIT_LCD)

static uint32_t zipit_lcd_transfer(SSIPeripheral *dev, uint32_t value)
{
    ZipitLCD *z = ZIPIT_LCD(dev);
    uint16_t val;

    trace_z2_lcd_reg_update(z->cur_reg, z->buf[0], z->buf[1], z->buf[2], value);
    if (z->selected) {
        z->buf[z->pos] = value & 0xff;
        z->pos++;
    }
    if (z->pos == 3) {
        switch (z->buf[0]) {
        case 0x74:
            z->cur_reg = z->buf[2];
            break;
        case 0x76:
            val = z->buf[1] << 8 | z->buf[2];
            if (z->cur_reg == 0x22 && val == 0x0000) {
                z->enabled = 1;
                trace_z2_lcd_enable_disable_result("enabled");
            } else if (z->cur_reg == 0x10 && val == 0x0000) {
                z->enabled = 0;
                trace_z2_lcd_enable_disable_result("disabled");
            }
            break;
        default:
            break;
        }
        z->pos = 0;
    }
    return 0;
}

static void z2_lcd_cs(void *opaque, int line, int level)
{
    ZipitLCD *z2_lcd = opaque;
    z2_lcd->selected = !level;
}

static void zipit_lcd_realize(SSIPeripheral *dev, Error **errp)
{
    ZipitLCD *z = ZIPIT_LCD(dev);
    z->selected = 0;
    z->enabled = 0;
    z->pos = 0;
}

static const VMStateDescription vmstate_zipit_lcd_state = {
    .name = "zipit-lcd",
    .version_id = 2,
    .minimum_version_id = 2,
    .fields = (const VMStateField[]) {
        VMSTATE_SSI_PERIPHERAL(ssidev, ZipitLCD),
        VMSTATE_INT32(selected, ZipitLCD),
        VMSTATE_INT32(enabled, ZipitLCD),
        VMSTATE_BUFFER(buf, ZipitLCD),
        VMSTATE_UINT32(cur_reg, ZipitLCD),
        VMSTATE_INT32(pos, ZipitLCD),
        VMSTATE_END_OF_LIST(),
    }
};

static void zipit_lcd_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    SSIPeripheralClass *k = SSI_PERIPHERAL_CLASS(klass);

    k->realize = zipit_lcd_realize;
    k->transfer = zipit_lcd_transfer;
    dc->vmsd = &vmstate_zipit_lcd_state;
}

static const TypeInfo zipit_lcd_info = {
    .name          = TYPE_ZIPIT_LCD,
    .parent        = TYPE_SSI_PERIPHERAL,
    .instance_size = sizeof(ZipitLCD),
    .class_init    = zipit_lcd_class_init,
};

#define TYPE_AER915 "aer915"
OBJECT_DECLARE_SIMPLE_TYPE(AER915State, AER915)

struct AER915State {
    I2CSlave parent_obj;

    int len;
    uint8_t buf[3];
};

static int aer915_send(I2CSlave *i2c, uint8_t data)
{
    AER915State *s = AER915(i2c);

    s->buf[s->len] = data;
    if (s->len++ > 2) {
        trace_z2_aer915_send_too_long(s->len);
        return 1;
    }

    if (s->len == 2) {
        trace_z2_aer915_send(s->buf[0], s->buf[1]);
    }

    return 0;
}

static int aer915_event(I2CSlave *i2c, enum i2c_event event)
{
    AER915State *s = AER915(i2c);

    trace_z2_aer915_event(s->len, event);
    switch (event) {
    case I2C_START_SEND:
        s->len = 0;
        break;
    case I2C_START_RECV:
        break;
    case I2C_FINISH:
        break;
    default:
        break;
    }

    return 0;
}

static uint8_t aer915_recv(I2CSlave *slave)
{
    AER915State *s = AER915(slave);
    int retval = 0x00;

    switch (s->buf[0]) {
    /* Return hardcoded battery voltage,
     * 0xf0 means ~4.1V
     */
    case 0x02:
        retval = 0xf0;
        break;
    /* Return 0x00 for other regs,
     * we don't know what they are for,
     * anyway they return 0x00 on real hardware.
     */
    default:
        break;
    }

    return retval;
}

static const VMStateDescription vmstate_aer915_state = {
    .name = "aer915",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (const VMStateField[]) {
        VMSTATE_INT32(len, AER915State),
        VMSTATE_BUFFER(buf, AER915State),
        VMSTATE_END_OF_LIST(),
    }
};

static void aer915_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    I2CSlaveClass *k = I2C_SLAVE_CLASS(klass);

    k->event = aer915_event;
    k->recv = aer915_recv;
    k->send = aer915_send;
    dc->vmsd = &vmstate_aer915_state;
}

static const TypeInfo aer915_info = {
    .name          = TYPE_AER915,
    .parent        = TYPE_I2C_SLAVE,
    .instance_size = sizeof(AER915State),
    .class_init    = aer915_class_init,
};

#define FLASH_SECTOR_SIZE   (64 * KiB)

static void z2_init(MachineState *machine)
{
    PXA2xxState *mpu;
    DriveInfo *dinfo;
    void *z2_lcd;
    I2CBus *bus;
    DeviceState *wm;
    I2CSlave *i2c_dev;

    /* Setup CPU & memory */
    mpu = pxa270_init(z2_binfo.ram_size, machine->cpu_type);

    dinfo = drive_get(IF_PFLASH, 0, 0);
    pflash_cfi01_register(Z2_FLASH_BASE, "z2.flash0", Z2_FLASH_SIZE,
                          dinfo ? blk_by_legacy_dinfo(dinfo) : NULL,
                          FLASH_SECTOR_SIZE, 4, 0, 0, 0, 0, 0);

    /* setup keypad */
    pxa27x_register_keypad(mpu->kp, map, 0x100);

    /* MMC/SD host */
    pxa2xx_mmci_handlers(mpu->mmc,
        NULL,
        qdev_get_gpio_in(mpu->gpio, Z2_GPIO_SD_DETECT));

    type_register_static(&zipit_lcd_info);
    type_register_static(&aer915_info);
    z2_lcd = ssi_create_peripheral(mpu->ssp[1], TYPE_ZIPIT_LCD);
    bus = pxa2xx_i2c_bus(mpu->i2c[0]);

    i2c_slave_create_simple(bus, TYPE_AER915, 0x55);

    i2c_dev = i2c_slave_new(TYPE_WM8750, 0x1b);
    wm = DEVICE(i2c_dev);

    if (machine->audiodev) {
        qdev_prop_set_string(wm, "audiodev", machine->audiodev);
    }
    i2c_slave_realize_and_unref(i2c_dev, bus, &error_abort);

    mpu->i2s->opaque = wm;
    mpu->i2s->codec_out = wm8750_dac_dat;
    mpu->i2s->codec_in = wm8750_adc_dat;
    wm8750_data_req_set(wm, mpu->i2s->data_req, mpu->i2s);

    qdev_connect_gpio_out(mpu->gpio, Z2_GPIO_LCD_CS,
                          qemu_allocate_irq(z2_lcd_cs, z2_lcd, 0));

    z2_binfo.board_id = 0x6dd;
    arm_load_kernel(mpu->cpu, machine, &z2_binfo);
}

static void z2_machine_init(MachineClass *mc)
{
    mc->desc = "Zipit Z2 (PXA27x)";
    mc->init = z2_init;
    mc->ignore_memory_transaction_failures = true;
    mc->default_cpu_type = ARM_CPU_TYPE_NAME("pxa270-c5");
    mc->deprecation_reason = "machine is old and unmaintained";

    machine_add_audiodev_property(mc);
}

DEFINE_MACHINE("z2", z2_machine_init)
