/*
 * QEMU AVR CPU
 *
 * Copyright (c) 2016 Michael Rolnik
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
 * License along with this library; if not, see
 * <http://www.gnu.org/licenses/lgpl-2.1.html>
 */

/*
 *  NOTE:
 *      This is not a real AVR board, this is an example!
 *      The CPU is an approximation of an ATmega2560, but is missing various
 *      built-in peripherals.
 *
 *      This example board loads provided binary file into flash memory and
 *      executes it from 0x00000000 address in the code memory space.
 *
 *      Currently used for AVR CPU validation
 *
 */

#include "qemu/osdep.h"
#include "qapi/error.h"
#include "qemu-common.h"
#include "cpu.h"
#include "hw/hw.h"
#include "sysemu/sysemu.h"
#include "sysemu/qtest.h"
#include "ui/console.h"
#include "hw/boards.h"
#include "hw/loader.h"
#include "qemu/error-report.h"
#include "exec/address-spaces.h"
#include "include/hw/sysbus.h"
#include "include/hw/char/avr_usart.h"
#include "include/hw/timer/avr_timer16.h"
#include "elf.h"

#define SIZE_FLASH 0x00040000
#define SIZE_SRAM 0x00002200
/*
 * Size of additional "external" memory, as if the AVR were configured to use
 * an external RAM chip.
 * Note that the configuration registers that normally enable this feature are
 * unimplemented.
 */
#define SIZE_EXMEM 0x00000000

/* Offsets of periphals in emulated memory space (i.e. not host addresses)  */
#define PRR0 0x64
#define PRR1 0x65
#define USART_BASE 0xc0
#define USART_PRR PRR0
#define USART_PRR_MASK 0b00000010
#define TIMER1_BASE 0x80
#define TIMER1_IMSK_BASE 0x6f
#define TIMER1_IFR_BASE 0x36
#define TIMER1_PRR PRR0
#define TIMER1_PRR_MASK 0b01000000

/* Interrupt numbers used by peripherals */
#define TIMER1_CAPT_IRQ 15
#define TIMER1_COMPA_IRQ 16
#define TIMER1_COMPB_IRQ 17
#define TIMER1_COMPC_IRQ 18
#define TIMER1_OVF_IRQ 19

static void sample_init(MachineState *machine)
{
    MemoryRegion *address_space_mem;
    MemoryRegion *ram;
    MemoryRegion *flash;
    AVRCPU *cpu_avr;
    const char *firmware = NULL;
    const char *filename;
    int bytes_loaded;
    AVRUsartState *usart0;
    AVRTimer16State *timer1;
    SysBusDevice *busdev;

    address_space_mem = get_system_memory();
    ram = g_new(MemoryRegion, 1);
    flash = g_new(MemoryRegion, 1);

    /* ATmega2560. */
    cpu_avr = AVR_CPU(cpu_create("avr6-avr"));

    memory_region_allocate_system_memory(
        ram, NULL, "avr.ram", SIZE_SRAM + SIZE_EXMEM);
    memory_region_add_subregion(address_space_mem, OFFSET_DATA, ram);

    memory_region_init_rom(flash, NULL, "avr.flash", SIZE_FLASH, &error_fatal);
    memory_region_add_subregion(address_space_mem, OFFSET_CODE, flash);

    /* USART 0 built-in peripheral */
    usart0 = AVR_USART(object_new(TYPE_AVR_USART));
    busdev = SYS_BUS_DEVICE(usart0);
    sysbus_mmio_map(busdev, 0, OFFSET_DATA + USART_BASE);
    /*
     * These IRQ numbers don't match the datasheet because we're counting from
     * zero and not including reset.
     */
    sysbus_connect_irq(busdev, 0, qdev_get_gpio_in(DEVICE(cpu_avr), 24));
    sysbus_connect_irq(busdev, 1, qdev_get_gpio_in(DEVICE(cpu_avr), 25));
    sysbus_connect_irq(busdev, 2, qdev_get_gpio_in(DEVICE(cpu_avr), 26));
    usart0->prr_address = OFFSET_DATA + PRR0;
    usart0->prr_mask = USART_PRR_MASK;
    qdev_prop_set_chr(DEVICE(usart0), "chardev", serial_hd(0));
    object_property_set_bool(OBJECT(usart0), true, "realized", &error_fatal);

    /* Timer 1 built-in periphal */
    timer1 = AVR_TIMER16(object_new(TYPE_AVR_TIMER16));
    busdev = SYS_BUS_DEVICE(timer1);
    sysbus_mmio_map(busdev, 0, OFFSET_DATA + TIMER1_BASE);
    sysbus_mmio_map(busdev, 1, OFFSET_DATA + TIMER1_IMSK_BASE);
    sysbus_mmio_map(busdev, 2, OFFSET_DATA + TIMER1_IFR_BASE);
    sysbus_connect_irq(busdev, 0, qdev_get_gpio_in(
        DEVICE(cpu_avr), TIMER1_CAPT_IRQ));
    sysbus_connect_irq(busdev, 1, qdev_get_gpio_in(
        DEVICE(cpu_avr), TIMER1_COMPA_IRQ));
    sysbus_connect_irq(busdev, 2, qdev_get_gpio_in(
        DEVICE(cpu_avr), TIMER1_COMPB_IRQ));
    sysbus_connect_irq(busdev, 3, qdev_get_gpio_in(
        DEVICE(cpu_avr), TIMER1_COMPC_IRQ));
    sysbus_connect_irq(busdev, 4, qdev_get_gpio_in(
        DEVICE(cpu_avr), TIMER1_OVF_IRQ));
    timer1->prr_address = OFFSET_DATA + TIMER1_PRR;
    timer1->prr_mask = TIMER1_PRR_MASK;
    object_property_set_bool(OBJECT(timer1), true, "realized", &error_fatal);

    /* Load firmware (contents of flash) trying to auto-detect format */
    firmware = machine->firmware;
    if (firmware != NULL) {
        filename = qemu_find_file(QEMU_FILE_TYPE_BIOS, firmware);
        if (filename == NULL) {
            error_report("Unable to find %s", firmware);
            exit(1);
        }

        bytes_loaded = load_elf(
            filename, NULL, NULL, NULL, NULL, NULL, NULL, 0, EM_NONE, 0, 0);
        if (bytes_loaded < 0) {
            error_report(
                "Unable to load %s as ELF, trying again as raw binary",
                firmware);
            bytes_loaded = load_image_targphys(
                filename, OFFSET_CODE, SIZE_FLASH);
        }
        if (bytes_loaded < 0) {
            error_report(
                "Unable to load firmware image %s as ELF or raw binary",
                firmware);
            exit(1);
        }
    }
}

static void sample_machine_init(MachineClass *mc)
{
    mc->desc = "AVR sample/example board";
    mc->init = sample_init;
    mc->is_default = 1;
}

DEFINE_MACHINE("sample", sample_machine_init)
