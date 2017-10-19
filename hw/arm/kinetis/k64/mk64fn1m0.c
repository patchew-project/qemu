/*
 * Kinetis K64 MK64FN1M0 microcontroller emulation.
 *
 * Copyright (c) 2017 Advantech Wireless
 * Written by Gabriel Costa <gabriel291075@gmail.com>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License version 2 or
 *  (at your option) any later version.
 */

#include "qemu/osdep.h"
#include "qapi/error.h"
#include "hw/sysbus.h"
#include "hw/ssi/ssi.h"
#include "hw/arm/arm.h"
#include "hw/devices.h"
#include "qemu/timer.h"
#include "hw/i2c/i2c.h"
#include "net/net.h"
#include "hw/boards.h"
#include "qemu/log.h"
#include "exec/address-spaces.h"
#include "sysemu/sysemu.h"
#include "hw/char/pl011.h"
#include "hw/misc/unimp.h"
#include "cpu.h"
#include "hw/arm/kinetis/k64/peripheral/sim.h"
#include "hw/arm/kinetis/k64/peripheral/mcg.h"
#include "hw/arm/kinetis/k64/peripheral/flextimer.h"
#include "hw/arm/kinetis/k64/peripheral/pmux.h"
#include "hw/arm/kinetis/k64/peripheral/uart.h"

#define FLASH_SIZE              1024*1024
#define FLASH_BASE_ADDRESS      0x00000000
#define SRAM_SIZE               192*1024
#define SRAM_BASE_ADDRESS       0x20000000

#define NUM_IRQ_LINES 85

/* System controller.  */

static void do_sys_reset(void *opaque, int n, int level)
{
    if (level) {
        qemu_system_reset_request(SHUTDOWN_CAUSE_GUEST_RESET);
    }
}

/* Interruptions at pag.77 K64P144M120F5RM.pdf */

static void mk64fn1m0_init_mach(MachineState *ms, const char *kernel_filename)
{
    DeviceState *nvic;

    MemoryRegion *system_memory = get_system_memory();
    MemoryRegion *sram = g_new(MemoryRegion, 1);
    MemoryRegion *flash = g_new(MemoryRegion, 1);

    memory_region_init_ram(flash, NULL, "k64.flash", FLASH_SIZE, &error_fatal);
    memory_region_set_readonly(flash, true);
    memory_region_add_subregion(system_memory, FLASH_BASE_ADDRESS, flash);

    memory_region_init_ram(sram, NULL, "k64.sram", SRAM_SIZE, &error_fatal);
    memory_region_add_subregion(system_memory, SRAM_BASE_ADDRESS, sram);

    nvic = armv7m_init(system_memory, FLASH_SIZE, NUM_IRQ_LINES,
            ms->kernel_filename, ms->cpu_type);
    
    qdev_connect_gpio_out_named(nvic, "SYSRESETREQ", 0,
            qemu_allocate_irq(&do_sys_reset, NULL, 0));

    sysbus_create_simple(TYPE_KINETIS_K64_SIM, 0x40048000, NULL);

    sysbus_create_simple(TYPE_KINETIS_K64_MCG, 0x40064000, NULL);
    
    sysbus_create_simple(TYPE_KINETIS_K64_PMUX, 0x40049000,
            qdev_get_gpio_in(nvic, 59));
    sysbus_create_simple(TYPE_KINETIS_K64_PMUX, 0x4004A000,
            qdev_get_gpio_in(nvic, 60));
    sysbus_create_simple(TYPE_KINETIS_K64_PMUX, 0x4004B000,
            qdev_get_gpio_in(nvic, 61));
    sysbus_create_simple(TYPE_KINETIS_K64_PMUX, 0x4004C000,
            qdev_get_gpio_in(nvic, 62));
    sysbus_create_simple(TYPE_KINETIS_K64_PMUX, 0x4004D000,
            qdev_get_gpio_in(nvic, 63));

    sysbus_create_simple(TYPE_KINETIS_K64_FLEXTIMER, 0x40038000,
            qdev_get_gpio_in(nvic, 42));
    sysbus_create_simple(TYPE_KINETIS_K64_FLEXTIMER, 0x40039000,
            qdev_get_gpio_in(nvic, 43));
    sysbus_create_simple(TYPE_KINETIS_K64_FLEXTIMER, 0x4003A000,
            qdev_get_gpio_in(nvic, 44));
    
/*    dev = sysbus_create_simple(TYPE_KINETIS_SPI, 0x4002C000,
            qdev_get_gpio_in(nvic, 31)); *SPI0*/
/*    dev = sysbus_create_simple(TYPE_KINETIS_SPI, 0x4002D000,
            qdev_get_gpio_in(nvic, 33)); *SPI1*/
/*    dev = sysbus_create_simple(TYPE_KINETIS_ADC, 0x4003B000,
            qdev_get_gpio_in(nvic, 31)); *ADC0*/
/*    dev = sysbus_create_simple(TYPE_KINETIS_DAC, 0x4002F000,
            qdev_get_gpio_in(nvic, 33)); *DAC0*/
/*    dev = sysbus_create_simple(TYPE_KINETIS_I2C, 0x40066000,
            qdev_get_gpio_in(nvic, 31)); *I2C0*/
/*    dev = sysbus_create_simple(TYPE_KINETIS_I2C, 0x40067000,
            qdev_get_gpio_in(nvic, 33)); *I2C1*/

//    sysbus_create_simple(TYPE_KINETIS_K64_UART, 0x4006A000,
//            qdev_get_gpio_in(nvic, 31)); /*UART0*/
    kinetis_k64_uart_create(0x4006A000, qdev_get_gpio_in(nvic, 31),
            serial_hds[0]);    
/*    dev = sysbus_create_simple(TYPE_KINETIS_K64_UART, 0x4006B000,
            qdev_get_gpio_in(nvic, 33)); *UART1*/
/*    dev = sysbus_create_simple(TYPE_KINETIS_K64_UART, 0x4006C000,
            qdev_get_gpio_in(nvic, 35)); *UART2*/
/*    dev = sysbus_create_simple(TYPE_KINETIS_K64_UART, 0x4006D000,
            qdev_get_gpio_in(nvic, 37)); *UART3*/

/*    dev = sysbus_create_simple(TYPE_KINETIS_SPI, 0x400AC000,
            qdev_get_gpio_in(nvic, 65)); *SPI2*/
/*    dev = sysbus_create_simple(TYPE_KINETIS_ADC, 0x400BB000,
            qdev_get_gpio_in(nvic, 73)); *ADC1*/
/*    dev = sysbus_create_simple(TYPE_KINETIS_I2C, 0x400E6000,
            qdev_get_gpio_in(nvic, 74)); *I2C2*/

/*    dev = sysbus_create_simple(TYPE_KINETIS_K64_UART, 0x400EA000,
            qdev_get_gpio_in(nvic, 66)); *UART4*/
/*    dev = sysbus_create_simple(TYPE_KINETIS_K64_UART, 0x400EB000,
            qdev_get_gpio_in(nvic, 68)); *UART5*/
   
    create_unimplemented_device("peripheral_bridge_0",  0x40000000, 0x1000);
    create_unimplemented_device("Crossbar_Switch",      0x40004000, 0x1000);
    create_unimplemented_device("DMA_Controller",       0x40008000, 0x1000);
    create_unimplemented_device("DMA_Controller_t",     0x40009000, 0x1000);
    create_unimplemented_device("FlexBus",              0x4000C000, 0x1000);
    create_unimplemented_device("MPU",                  0x4000D000, 0x1000);
    create_unimplemented_device("Flash_mem_ctrl",       0x4001F000, 0x1000);
    create_unimplemented_device("Flash_mem",            0x40020000, 0x1000);
    create_unimplemented_device("DMA_ch_multiplexer",   0x40021000, 0x1000);
}

static void mk64fn1m0_init(MachineState *machine)
{
    const char *kernel_filename = machine->kernel_filename;
    mk64fn1m0_init_mach(machine, kernel_filename);
}

static void mk64fn1m0_class_init(ObjectClass *oc, void *data)
{
    MachineClass *mc = MACHINE_CLASS(oc);

    mc->desc = "Kinetis K64 MCU (Cortex-M4)";
    mc->init = mk64fn1m0_init;
    mc->ignore_memory_transaction_failures = true;
    mc->default_cpu_type = ARM_CPU_TYPE_NAME("cortex-m4");
    mc->max_cpus = 1;
}

static const TypeInfo mk64_type = {
    .name = MACHINE_TYPE_NAME("mk64fn1m0"),
    .parent = TYPE_MACHINE,
    .class_init = mk64fn1m0_class_init,
};

static void mk64fn1m0_machine_init(void)
{
    type_register_static(&mk64_type);
}

type_init(mk64fn1m0_machine_init)