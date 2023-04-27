/*
 * QEMU RISC-V Board Compatible with OpenTitan EarlGrey FPGA platform
 *
 * Copyright (c) 2022-2023 Rivos, Inc.
 *
 * Author(s):
 *  Emmanuel Blot <eblot@rivosinc.com>
 *  Loïc Lefort <loic@rivosinc.com>
 *
 * This implementation is based on OpenTitan RTL version:
 *  <lowRISC/opentitan@caa3bd0a14ddebbf60760490f7c917901482c8fd>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 */

#include "qemu/osdep.h"
#include "qemu/cutils.h"
#include "qemu/units.h"
#include "qapi/error.h"
#include "cpu.h"
#include "system/address-spaces.h"
#include "hw/core/boards.h"
#include "hw/intc/sifive_plic.h"
#include "hw/misc/unimp.h"
#include "hw/core/qdev-properties.h"
#include "hw/riscv/ibex_common.h"
#include "hw/riscv/ot_earlgrey.h"
#include "system/system.h"

/* ------------------------------------------------------------------------ */
/* Constants */
/* ------------------------------------------------------------------------ */

#define OT_EARLGREY_PERIPHERAL_CLK_HZ 2500000u

enum OtEarlgreySocMemory {
    OT_EARLGREY_SOC_MEM_ROM,
    OT_EARLGREY_SOC_MEM_RAM,
    OT_EARLGREY_SOC_MEM_FLASH,
};

static const MemMapEntry ot_earlgrey_soc_memories[] = {
    [OT_EARLGREY_SOC_MEM_ROM] = { 0x00008000u, 0x8000u },
    [OT_EARLGREY_SOC_MEM_RAM] = { 0x10000000u, 0x20000u },
    [OT_EARLGREY_SOC_MEM_FLASH] = { 0x20000000u, 0x100000u },
};

enum OtEarlgreySocDevice {
    OT_EARLGREY_SOC_DEV_ADC_CTRL,
    OT_EARLGREY_SOC_DEV_AES,
    OT_EARLGREY_SOC_DEV_ALERT_HANDLER,
    OT_EARLGREY_SOC_DEV_AON_TIMER,
    OT_EARLGREY_SOC_DEV_AST,
    OT_EARLGREY_SOC_DEV_CLKMGR,
    OT_EARLGREY_SOC_DEV_CSRNG,
    OT_EARLGREY_SOC_DEV_EDN0,
    OT_EARLGREY_SOC_DEV_EDN1,
    OT_EARLGREY_SOC_DEV_ENTROPY_SRC,
    OT_EARLGREY_SOC_DEV_FLASH_CTRL,
    OT_EARLGREY_SOC_DEV_FLASH_CTRL_PRIM,
    OT_EARLGREY_SOC_DEV_GPIO,
    OT_EARLGREY_SOC_DEV_HART,
    OT_EARLGREY_SOC_DEV_HMAC,
    OT_EARLGREY_SOC_DEV_I2C0,
    OT_EARLGREY_SOC_DEV_I2C1,
    OT_EARLGREY_SOC_DEV_I2C2,
    OT_EARLGREY_SOC_DEV_IBEX_WRAPPER,
    OT_EARLGREY_SOC_DEV_KEYMGR,
    OT_EARLGREY_SOC_DEV_KMAC,
    OT_EARLGREY_SOC_DEV_LC_CTRL,
    OT_EARLGREY_SOC_DEV_OTBN,
    OT_EARLGREY_SOC_DEV_OTP_CTRL,
    OT_EARLGREY_SOC_DEV_OTP_CTRL_PRIM,
    OT_EARLGREY_SOC_DEV_PATTGEN,
    OT_EARLGREY_SOC_DEV_PINMUX,
    OT_EARLGREY_SOC_DEV_PLIC,
    OT_EARLGREY_SOC_DEV_PWM,
    OT_EARLGREY_SOC_DEV_PWRMGR,
    OT_EARLGREY_SOC_DEV_RAM_RET,
    OT_EARLGREY_SOC_DEV_ROM_CTRL,
    OT_EARLGREY_SOC_DEV_RSTMGR,
    OT_EARLGREY_SOC_DEV_RV_DM,
    OT_EARLGREY_SOC_DEV_RV_DM_MEM,
    OT_EARLGREY_SOC_DEV_SENSOR_CTRL,
    OT_EARLGREY_SOC_DEV_SPI_DEVICE,
    OT_EARLGREY_SOC_DEV_SPI_HOST0,
    OT_EARLGREY_SOC_DEV_SPI_HOST1,
    OT_EARLGREY_SOC_DEV_SRAM_CTRL,
    OT_EARLGREY_SOC_DEV_SRAM_CTRL_MAIN,
    OT_EARLGREY_SOC_DEV_SYSRST_CTRL,
    OT_EARLGREY_SOC_DEV_TIMER,
    OT_EARLGREY_SOC_DEV_UART0,
    OT_EARLGREY_SOC_DEV_UART1,
    OT_EARLGREY_SOC_DEV_UART2,
    OT_EARLGREY_SOC_DEV_UART3,
    OT_EARLGREY_SOC_DEV_USBDEV,
};

#define OT_EARLGREY_SOC_GPIO(_irq_, _target_, _num_) \
    IBEX_GPIO(_irq_, OT_EARLGREY_SOC_DEV_##_target_, _num_)

#define OT_EARLGREY_SOC_GPIO_SYSBUS_IRQ(_irq_, _target_, _num_) \
    IBEX_GPIO_SYSBUS_IRQ(_irq_, OT_EARLGREY_SOC_DEV_##_target_, _num_)

#define OT_EARLGREY_SOC_DEVLINK(_pname_, _target_) \
    IBEX_DEVLINK(_pname_, OT_EARLGREY_SOC_DEV_##_target_)

/*
 * MMIO/interrupt mapping as per:
 * lowRISC/opentitan: hw/top_earlgrey/sw/autogen/top_earlgrey_memory.h
 * and
 * lowRISC/opentitan: hw/top_earlgrey/sw/autogen/top_earlgrey.h
 */
static const IbexDeviceDef ot_earlgrey_soc_devices[] = {
    /* clang-format off */
    [OT_EARLGREY_SOC_DEV_HART] = {
        .type = TYPE_RISCV_CPU_LOWRISC_OPENTITAN,
        .prop = IBEXDEVICEPROPDEFS(
            IBEX_DEV_BOOL_PROP("zba", true),
            IBEX_DEV_BOOL_PROP("zbb", true),
            IBEX_DEV_BOOL_PROP("zbc", true),
            IBEX_DEV_BOOL_PROP("zbs", true),
            IBEX_DEV_BOOL_PROP("smepmp", true)
        ),
    },
    [OT_EARLGREY_SOC_DEV_RV_DM_MEM] = {
        .type = TYPE_UNIMPLEMENTED_DEVICE,
        .name = "ot-rv_dm_mem",
        .cfg = &ibex_unimp_configure,
        .memmap = MEMMAPENTRIES(
            { 0x00010000u, 0x1000u }
        ),
    },
    [OT_EARLGREY_SOC_DEV_UART0] = {
        .type = TYPE_UNIMPLEMENTED_DEVICE,
        .name = "ot-uart",
        .cfg = &ibex_unimp_configure,
        .instance = 0,
        .memmap = MEMMAPENTRIES(
            { 0x40000000u, 0x40u }
        ),
    },
    [OT_EARLGREY_SOC_DEV_UART1] = {
        .type = TYPE_UNIMPLEMENTED_DEVICE,
        .name = "ot-uart",
        .cfg = &ibex_unimp_configure,
        .instance = 1,
        .memmap = MEMMAPENTRIES(
            { 0x40010000u, 0x40u }
        ),
    },
    [OT_EARLGREY_SOC_DEV_UART2] = {
        .type = TYPE_UNIMPLEMENTED_DEVICE,
        .name = "ot-uart",
        .cfg = &ibex_unimp_configure,
        .instance = 2,
        .memmap = MEMMAPENTRIES(
            { 0x40020000u, 0x40u }
        ),
    },
    [OT_EARLGREY_SOC_DEV_UART3] = {
        .type = TYPE_UNIMPLEMENTED_DEVICE,
        .name = "ot-uart",
        .cfg = &ibex_unimp_configure,
        .instance = 3,
        .memmap = MEMMAPENTRIES(
            { 0x40030000u, 0x1000u }
        ),
    },
    [OT_EARLGREY_SOC_DEV_GPIO] = {
        .type = TYPE_UNIMPLEMENTED_DEVICE,
        .name = "ot-gpio",
        .cfg = &ibex_unimp_configure,
        .memmap = MEMMAPENTRIES(
            { 0x40040000u, 0x40u }
        ),
    },
    [OT_EARLGREY_SOC_DEV_SPI_DEVICE] = {
        .type = TYPE_UNIMPLEMENTED_DEVICE,
        .name = "ot-spi_device",
        .cfg = &ibex_unimp_configure,
        .memmap = MEMMAPENTRIES(
            { 0x40050000u, 0x2000u }
        ),
    },
    [OT_EARLGREY_SOC_DEV_I2C0] = {
        .type = TYPE_UNIMPLEMENTED_DEVICE,
        .name = "ot-i2c",
        .cfg = &ibex_unimp_configure,
        .instance = 0,
        .memmap = MEMMAPENTRIES(
            { 0x40080000u, 0x80u }
        ),
    },
    [OT_EARLGREY_SOC_DEV_I2C1] = {
        .type = TYPE_UNIMPLEMENTED_DEVICE,
        .name = "ot-i2c",
        .cfg = &ibex_unimp_configure,
        .instance = 1,
        .memmap = MEMMAPENTRIES(
            { 0x40090000u, 0x80u }
        ),
    },
    [OT_EARLGREY_SOC_DEV_I2C2] = {
        .type = TYPE_UNIMPLEMENTED_DEVICE,
        .name = "ot-i2c",
        .cfg = &ibex_unimp_configure,
        .instance = 2,
        .memmap = MEMMAPENTRIES(
            { 0x400a0000u, 0x80u }
        ),
    },
    [OT_EARLGREY_SOC_DEV_PATTGEN] = {
        .type = TYPE_UNIMPLEMENTED_DEVICE,
        .name = "ot-pattgen",
        .cfg = &ibex_unimp_configure,
        .memmap = MEMMAPENTRIES(
            { 0x400e0000u, 0x40u }
        ),
    },
    [OT_EARLGREY_SOC_DEV_TIMER] = {
        .type = TYPE_UNIMPLEMENTED_DEVICE,
        .name = "ot-timer",
        .cfg = &ibex_unimp_configure,
        .memmap = MEMMAPENTRIES(
            { 0x40100000u, 0x200u }
        ),
    },
    [OT_EARLGREY_SOC_DEV_OTP_CTRL] = {
        .type = TYPE_UNIMPLEMENTED_DEVICE,
        .name = "ot-otp_ctrl",
        .cfg = &ibex_unimp_configure,
        .memmap = MEMMAPENTRIES(
            { 0x40130000u, 0x2000u }
        ),
    },
    [OT_EARLGREY_SOC_DEV_OTP_CTRL_PRIM] = {
        .type = TYPE_UNIMPLEMENTED_DEVICE,
        .name = "ot-ot_ctrl_prim",
        .cfg = &ibex_unimp_configure,
        .memmap = MEMMAPENTRIES(
            { 0x40132000u, 0x20u }
        ),
    },
    [OT_EARLGREY_SOC_DEV_LC_CTRL] = {
        .type = TYPE_UNIMPLEMENTED_DEVICE,
        .name = "ot-lc_ctrl",
        .cfg = &ibex_unimp_configure,
        .memmap = MEMMAPENTRIES(
            { 0x40140000u, 0x100u }
        ),
    },
    [OT_EARLGREY_SOC_DEV_ALERT_HANDLER] = {
        .type = TYPE_UNIMPLEMENTED_DEVICE,
        .name = "ot-alert_handler",
        .cfg = &ibex_unimp_configure,
        .memmap = MEMMAPENTRIES(
            { 0x40150000u, 0x800u }
        ),
    },
    [OT_EARLGREY_SOC_DEV_SPI_HOST0] = {
        .type = TYPE_UNIMPLEMENTED_DEVICE,
        .name = "ot-spi_host",
        .cfg = &ibex_unimp_configure,
        .instance = 0,
        .memmap = MEMMAPENTRIES(
            { 0x40300000u, 0x40u }
        ),
    },
    [OT_EARLGREY_SOC_DEV_SPI_HOST1] = {
        .type = TYPE_UNIMPLEMENTED_DEVICE,
        .name = "ot-spi_host",
        .cfg = &ibex_unimp_configure,
        .instance = 1,
        .memmap = MEMMAPENTRIES(
            { 0x40310000u, 0x40u }
        ),
    },
    [OT_EARLGREY_SOC_DEV_USBDEV] = {
        .type = TYPE_UNIMPLEMENTED_DEVICE,
        .name = "ot-usbdev",
        .cfg = &ibex_unimp_configure,
        .memmap = MEMMAPENTRIES(
            { 0x40320000u, 0x1000u }
        ),
    },
    [OT_EARLGREY_SOC_DEV_PWRMGR] = {
        .type = TYPE_UNIMPLEMENTED_DEVICE,
        .name = "ot-pwrmgr",
        .cfg = &ibex_unimp_configure,
        .memmap = MEMMAPENTRIES(
            { 0x40400000u, 0x80u }
        ),
    },
    [OT_EARLGREY_SOC_DEV_RSTMGR] = {
        .type = TYPE_UNIMPLEMENTED_DEVICE,
        .name = "ot-rstmgr",
        .cfg = &ibex_unimp_configure,
        .memmap = MEMMAPENTRIES(
            { 0x40410000u, 0x80u }
        ),
    },
    [OT_EARLGREY_SOC_DEV_CLKMGR] = {
        .type = TYPE_UNIMPLEMENTED_DEVICE,
        .name = "ot-clkmgr",
        .cfg = &ibex_unimp_configure,
        .memmap = MEMMAPENTRIES(
            { 0x40420000u, 0x80u }
        ),
    },
    [OT_EARLGREY_SOC_DEV_SYSRST_CTRL] = {
        .type = TYPE_UNIMPLEMENTED_DEVICE,
        .name = "ot-sysrst_ctrl",
        .cfg = &ibex_unimp_configure,
        .memmap = MEMMAPENTRIES(
            { 0x40430000u, 0x100u }
        ),
    },
    [OT_EARLGREY_SOC_DEV_ADC_CTRL] = {
        .type = TYPE_UNIMPLEMENTED_DEVICE,
        .name = "ot-adc_ctrl",
        .cfg = &ibex_unimp_configure,
        .memmap = MEMMAPENTRIES(
            { 0x40440000u, 0x80u }
        ),
    },
    [OT_EARLGREY_SOC_DEV_PWM] = {
        .type = TYPE_UNIMPLEMENTED_DEVICE,
        .name = "ot-pwm",
        .cfg = &ibex_unimp_configure,
        .memmap = MEMMAPENTRIES(
            { 0x40450000u, 0x80u }
        ),
    },
    [OT_EARLGREY_SOC_DEV_PINMUX] = {
        .type = TYPE_UNIMPLEMENTED_DEVICE,
        .name = "ot-pinmux",
        .cfg = &ibex_unimp_configure,
        .memmap = MEMMAPENTRIES(
            { 0x40460000u, 0x1000u }
        ),
    },
    [OT_EARLGREY_SOC_DEV_AON_TIMER] = {
        .type = TYPE_UNIMPLEMENTED_DEVICE,
        .name = "ot-aon_timer",
        .cfg = &ibex_unimp_configure,
        .memmap = MEMMAPENTRIES(
            { 0x40470000u, 0x40u }
        ),
    },
    [OT_EARLGREY_SOC_DEV_AST] = {
        .type = TYPE_UNIMPLEMENTED_DEVICE,
        .name = "ot-ast",
        .cfg = &ibex_unimp_configure,
        .memmap = MEMMAPENTRIES(
            { 0x40480000u, 0x400u }
        ),
    },
    [OT_EARLGREY_SOC_DEV_SENSOR_CTRL] = {
        .type = TYPE_UNIMPLEMENTED_DEVICE,
        .name = "ot-sensor_ctrl",
        .cfg = &ibex_unimp_configure,
        .memmap = MEMMAPENTRIES(
            { 0x40490000u, 0x40u }
        ),
    },
    [OT_EARLGREY_SOC_DEV_SRAM_CTRL] = {
        .type = TYPE_UNIMPLEMENTED_DEVICE,
        .name = "ot-sram_ctrl",
        .cfg = &ibex_unimp_configure,
        .memmap = MEMMAPENTRIES(
            { 0x40500000u, 0x20u }
        ),
    },
    [OT_EARLGREY_SOC_DEV_RAM_RET] = {
        .type = TYPE_UNIMPLEMENTED_DEVICE,
        .name = "ot-ram_ret",
        .cfg = &ibex_unimp_configure,
        .memmap = MEMMAPENTRIES(
            { 0x40600000u, 0x1000u }
        ),
    },
    [OT_EARLGREY_SOC_DEV_FLASH_CTRL] = {
        .type = TYPE_UNIMPLEMENTED_DEVICE,
        .name = "ot-flash_ctrl",
        .cfg = &ibex_unimp_configure,
        .memmap = MEMMAPENTRIES(
            { 0x41000000u, 0x200u }
        ),
    },
    [OT_EARLGREY_SOC_DEV_FLASH_CTRL_PRIM] = {
        .type = TYPE_UNIMPLEMENTED_DEVICE,
        .name = "ot-flash_ctrl_prim",
        .cfg = &ibex_unimp_configure,
        .memmap = MEMMAPENTRIES(
            { 0x41008000u, 0x80u }
        ),
    },
    [OT_EARLGREY_SOC_DEV_AES] = {
        .type = TYPE_UNIMPLEMENTED_DEVICE,
        .name = "ot-aes",
        .cfg = &ibex_unimp_configure,
        .memmap = MEMMAPENTRIES(
            { 0x41100000u, 0x100u }
        ),
    },
    [OT_EARLGREY_SOC_DEV_HMAC] = {
        .type = TYPE_UNIMPLEMENTED_DEVICE,
        .name = "ot-hmac",
        .cfg = &ibex_unimp_configure,
        .memmap = MEMMAPENTRIES(
            { 0x41110000u, 0x1000u }
        ),
    },
    [OT_EARLGREY_SOC_DEV_KMAC] = {
        .type = TYPE_UNIMPLEMENTED_DEVICE,
        .name = "ot-kmac",
        .cfg = &ibex_unimp_configure,
        .memmap = MEMMAPENTRIES(
            { 0x41120000u, 0x1000u }
        ),
    },
    [OT_EARLGREY_SOC_DEV_OTBN] = {
        .type = TYPE_UNIMPLEMENTED_DEVICE,
        .name = "ot-otbn",
        .cfg = &ibex_unimp_configure,
        .memmap = MEMMAPENTRIES(
            { 0x41130000u, 0x10000u }
        ),
    },
    [OT_EARLGREY_SOC_DEV_KEYMGR] = {
        .type = TYPE_UNIMPLEMENTED_DEVICE,
        .name = "ot-keymgr",
        .cfg = &ibex_unimp_configure,
        .memmap = MEMMAPENTRIES(
            { 0x41140000u, 0x100u }
        ),
    },
    [OT_EARLGREY_SOC_DEV_CSRNG] = {
        .type = TYPE_UNIMPLEMENTED_DEVICE,
        .name = "ot-csrng",
        .cfg = &ibex_unimp_configure,
        .memmap = MEMMAPENTRIES(
            { 0x41150000u, 0x80u }
        ),
    },
    [OT_EARLGREY_SOC_DEV_ENTROPY_SRC] = {
        .type = TYPE_UNIMPLEMENTED_DEVICE,
        .name = "ot-entropy_src",
        .cfg = &ibex_unimp_configure,
        .memmap = MEMMAPENTRIES(
            { 0x41160000u, 0x100u }
        ),
    },
    [OT_EARLGREY_SOC_DEV_EDN0] = {
        .type = TYPE_UNIMPLEMENTED_DEVICE,
        .name = "ot-edn",
        .cfg = &ibex_unimp_configure,
        .instance = 0,
        .memmap = MEMMAPENTRIES(
            { 0x41170000u, 0x80u }
        ),
    },
    [OT_EARLGREY_SOC_DEV_EDN1] = {
        .type = TYPE_UNIMPLEMENTED_DEVICE,
        .name = "ot-edn",
        .cfg = &ibex_unimp_configure,
        .instance = 1,
        .memmap = MEMMAPENTRIES(
            { 0x41180000u, 0x80u }
        ),
    },
    [OT_EARLGREY_SOC_DEV_SRAM_CTRL_MAIN] = {
        .type = TYPE_UNIMPLEMENTED_DEVICE,
        .name = "ot-sram_ctrl_main",
        .cfg = &ibex_unimp_configure,
        .memmap = MEMMAPENTRIES(
            { 0x411c0000u, 0x20u }
        ),
    },
    [OT_EARLGREY_SOC_DEV_ROM_CTRL] = {
        .type = TYPE_UNIMPLEMENTED_DEVICE,
        .name = "ot-rom_ctrl",
        .cfg = &ibex_unimp_configure,
        .memmap = MEMMAPENTRIES(
            { 0x411e0000u, 0x80u }
        ),
    },
    [OT_EARLGREY_SOC_DEV_IBEX_WRAPPER] = {
        .type = TYPE_UNIMPLEMENTED_DEVICE,
        .name = "ot-ibex_wrapper",
        .cfg = &ibex_unimp_configure,
        .memmap = MEMMAPENTRIES(
            { 0x411f0000u, 0x100u }
        ),
    },
    [OT_EARLGREY_SOC_DEV_RV_DM] = {
        .type = TYPE_UNIMPLEMENTED_DEVICE,
        .name = "ot-rv_dm",
        .cfg = &ibex_unimp_configure,
        .memmap = MEMMAPENTRIES(
            { 0x41200000u, 0x4u }
        ),
    },
    [OT_EARLGREY_SOC_DEV_PLIC] = {
        .type = TYPE_SIFIVE_PLIC,
        .memmap = MEMMAPENTRIES(
            { 0x48000000u, 0x8000000u }
        ),
        .gpio = IBEXGPIOCONNDEFS(
            OT_EARLGREY_SOC_GPIO(1, HART, IRQ_M_EXT)
        ),
        .prop = IBEXDEVICEPROPDEFS(
            IBEX_DEV_STRING_PROP("hart-config", "M"),
            IBEX_DEV_UINT_PROP("hartid-base", 0u),
            /* note: should always be max_irq + 1 */
            IBEX_DEV_UINT_PROP("num-sources", 185u),
            IBEX_DEV_UINT_PROP("num-priorities", 3u),
            IBEX_DEV_UINT_PROP("priority-base", 0x0u),
            IBEX_DEV_UINT_PROP("pending-base", 0x1000u),
            IBEX_DEV_UINT_PROP("enable-base", 0x2000u),
            IBEX_DEV_UINT_PROP("enable-stride", 32u),
            IBEX_DEV_UINT_PROP("context-base", 0x200000u),
            IBEX_DEV_UINT_PROP("context-stride", 8u),
            IBEX_DEV_UINT_PROP("aperture-size", 0x8000000u)
        ),
    },
    /* clang-format on */
};

enum OtEarlgreyBoardDevice {
    OT_EARLGREY_BOARD_DEV_SOC,
    _OT_EARLGREY_BOARD_DEV_COUNT,
};

/* ------------------------------------------------------------------------ */
/* Type definitions */
/* ------------------------------------------------------------------------ */

struct OtEarlGreySoCState {
    SysBusDevice parent_obj;

    DeviceState **devices;
    MemoryRegion *memories;
};

struct OtEarlGreyBoardState {
    DeviceState parent_obj;

    DeviceState **devices;
};

struct OtEarlGreyMachineState {
    MachineState parent_obj;
};

/* ------------------------------------------------------------------------ */
/* SoC */
/* ------------------------------------------------------------------------ */

static void ot_earlgrey_soc_reset(DeviceState *dev)
{
    OtEarlGreySoCState *s = RISCV_OT_EARLGREY_SOC(dev);

    cpu_reset(CPU(s->devices[OT_EARLGREY_SOC_DEV_HART]));
}

static void ot_earlgrey_soc_realize(DeviceState *dev, Error **errp)
{
    OtEarlGreySoCState *s = RISCV_OT_EARLGREY_SOC(dev);
    const MemMapEntry *memmap = &ot_earlgrey_soc_memories[0];

    MachineState *ms = MACHINE(qdev_get_machine());
    MemoryRegion *sys_mem = get_system_memory();

    /* RAM */
    memory_region_add_subregion(sys_mem, memmap[OT_EARLGREY_SOC_MEM_RAM].base,
                                ms->ram);

    /* Boot ROM */
    memory_region_init_rom(&s->memories[OT_EARLGREY_SOC_MEM_ROM], OBJECT(dev),
                           "ot-rom", memmap[OT_EARLGREY_SOC_MEM_ROM].size,
                           &error_fatal);
    memory_region_add_subregion(sys_mem, memmap[OT_EARLGREY_SOC_MEM_ROM].base,
                                &s->memories[OT_EARLGREY_SOC_MEM_ROM]);

    /* Flash memory */
    memory_region_init_rom(&s->memories[OT_EARLGREY_SOC_MEM_FLASH], OBJECT(dev),
                           "ot-flash", memmap[OT_EARLGREY_SOC_MEM_FLASH].size,
                           &error_fatal);
    memory_region_add_subregion(sys_mem, memmap[OT_EARLGREY_SOC_MEM_FLASH].base,
                                &s->memories[OT_EARLGREY_SOC_MEM_FLASH]);

    /* Link, define properties and realize devices, then connect GPIOs */
    ibex_link_devices(s->devices, ot_earlgrey_soc_devices,
                      ARRAY_SIZE(ot_earlgrey_soc_devices));
    ibex_define_device_props(s->devices, ot_earlgrey_soc_devices,
                             ARRAY_SIZE(ot_earlgrey_soc_devices));
    ibex_realize_system_devices(s->devices, ot_earlgrey_soc_devices,
                                ARRAY_SIZE(ot_earlgrey_soc_devices));
    ibex_connect_devices(s->devices, ot_earlgrey_soc_devices,
                         ARRAY_SIZE(ot_earlgrey_soc_devices));

    /* load kernel if provided */
    ibex_load_kernel(NULL);
}

static void ot_earlgrey_soc_init(Object *obj)
{
    OtEarlGreySoCState *s = RISCV_OT_EARLGREY_SOC(obj);

    s->devices =
        ibex_create_devices(ot_earlgrey_soc_devices,
                            ARRAY_SIZE(ot_earlgrey_soc_devices), DEVICE(s));
    s->memories = g_new0(MemoryRegion, ARRAY_SIZE(ot_earlgrey_soc_memories));
}

static void ot_earlgrey_soc_class_init(ObjectClass *oc, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(oc);

    dc->legacy_reset = &ot_earlgrey_soc_reset;
    dc->realize = &ot_earlgrey_soc_realize;
    dc->user_creatable = false;
}

static const TypeInfo ot_earlgrey_soc_type_info = {
    .name = TYPE_RISCV_OT_EARLGREY_SOC,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(OtEarlGreySoCState),
    .instance_init = &ot_earlgrey_soc_init,
    .class_init = &ot_earlgrey_soc_class_init,
};

static void ot_earlgrey_soc_register_types(void)
{
    type_register_static(&ot_earlgrey_soc_type_info);
}

type_init(ot_earlgrey_soc_register_types);

/* ------------------------------------------------------------------------ */
/* Board */
/* ------------------------------------------------------------------------ */

static void ot_earlgrey_board_realize(DeviceState *dev, Error **errp)
{
    OtEarlGreyBoardState *board = RISCV_OT_EARLGREY_BOARD(dev);

    DeviceState *soc = board->devices[OT_EARLGREY_BOARD_DEV_SOC];
    object_property_add_child(OBJECT(board), "soc", OBJECT(soc));
    sysbus_realize_and_unref(SYS_BUS_DEVICE(soc), &error_fatal);
}

static void ot_earlgrey_board_init(Object *obj)
{
    OtEarlGreyBoardState *s = RISCV_OT_EARLGREY_BOARD(obj);

    s->devices = g_new0(DeviceState *, _OT_EARLGREY_BOARD_DEV_COUNT);
    s->devices[OT_EARLGREY_BOARD_DEV_SOC] =
        qdev_new(TYPE_RISCV_OT_EARLGREY_SOC);
}

static void ot_earlgrey_board_class_init(ObjectClass *oc, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(oc);

    dc->realize = &ot_earlgrey_board_realize;
}

static const TypeInfo ot_earlgrey_board_type_info = {
    .name = TYPE_RISCV_OT_EARLGREY_BOARD,
    .parent = TYPE_DEVICE,
    .instance_size = sizeof(OtEarlGreyBoardState),
    .instance_init = &ot_earlgrey_board_init,
    .class_init = &ot_earlgrey_board_class_init,
};

static void ot_earlgrey_board_register_types(void)
{
    type_register_static(&ot_earlgrey_board_type_info);
}

type_init(ot_earlgrey_board_register_types);

/* ------------------------------------------------------------------------ */
/* Machine */
/* ------------------------------------------------------------------------ */

static void ot_earlgrey_machine_instance_init(Object *obj)
{
    OtEarlGreyMachineState *s = RISCV_OT_EARLGREY_MACHINE(obj);

    /* nothing here */
    (void)s;
}

static void ot_earlgrey_machine_init(MachineState *state)
{
    DeviceState *dev = qdev_new(TYPE_RISCV_OT_EARLGREY_BOARD);

    object_property_add_child(OBJECT(state), "board", OBJECT(dev));
    qdev_realize(dev, NULL, &error_fatal);
}

static void ot_earlgrey_machine_class_init(ObjectClass *oc, const void *data)
{
    MachineClass *mc = MACHINE_CLASS(oc);

    mc->desc = "RISC-V Board compatible with OpenTitan EarlGrey FPGA platform";
    mc->init = ot_earlgrey_machine_init;
    mc->max_cpus = 1u;
    mc->default_cpu_type =
        ot_earlgrey_soc_devices[OT_EARLGREY_SOC_DEV_HART].type;
    mc->default_ram_id = "ot-ram";
    mc->default_ram_size =
        ot_earlgrey_soc_memories[OT_EARLGREY_SOC_MEM_RAM].size;
}

static const TypeInfo ot_earlgrey_machine_type_info = {
    .name = TYPE_RISCV_OT_EARLGREY_MACHINE,
    .parent = TYPE_MACHINE,
    .instance_size = sizeof(OtEarlGreyMachineState),
    .instance_init = &ot_earlgrey_machine_instance_init,
    .class_init = &ot_earlgrey_machine_class_init,
};

static void ot_earlgrey_machine_register_types(void)
{
    type_register_static(&ot_earlgrey_machine_type_info);
}

type_init(ot_earlgrey_machine_register_types);
