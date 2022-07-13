/*
 * Designware System-on-Chip general purpose input/output register definition
 *
 * Copyright 2022 SiFive, Inc.
 * Copyright (c) 2011 Jamie Iles
 *
 * This code is licensed under the GPL version 2 or later.  See
 * the COPYING file in the top-level directory.
*/

#ifndef DESIGNWARE_GPIO_H
#define DESIGNWARE_GPIO_H

#include "hw/sysbus.h"
#include "qom/object.h"

#define TYPE_DESIGNWARE_GPIO "designware.gpio"
typedef struct DESIGNWAREGPIOState DESIGNWAREGPIOState;
DECLARE_INSTANCE_CHECKER(DESIGNWAREGPIOState, DESIGNWARE_GPIO,
                         TYPE_DESIGNWARE_GPIO)

/* maximum pins for 4 banks */
#define DESIGNWARE_GPIO_BANKS (4)
#define DESIGNWARE_GPIO_NR_PER_BANK (32)
#define DESIGNWARE_GPIO_PINS (32 * DESIGNWARE_GPIO_BANKS)

#define DESIGNWARE_GPIO_SIZE 0x100

/* registers copied from linux driver */
#define REG_SWPORTA_DR		0x00
#define REG_SWPORTA_DDR         0x04
#define REG_SWPORTB_DR		0x0c
#define REG_SWPORTB_DDR         0x10
#define REG_SWPORTC_DR		0x18
#define REG_SWPORTC_DDR         0x1c
#define REG_SWPORTD_DR		0x24
#define REG_SWPORTD_DDR         0x28
#define REG_INTEN		0x30
#define REG_INTMASK		0x34
#define REG_INTTYPE_LEVEL	0x38
#define REG_INT_POLARITY	0x3c
#define REG_INTSTATUS		0x40
#define REG_INTSTATUS_RAW       0x44
#define REG_PORTA_DEBOUNCE	0x48
#define REG_PORTA_EOI		0x4c    /* write to clear edge irq */
#define REG_EXT_PORTA		0x50
#define REG_EXT_PORTB		0x54
#define REG_EXT_PORTC		0x58
#define REG_EXT_PORTD		0x5c
#define REG_ID                  0x64

#define REG_EXT_PORT_STRIDE	0x04 /* register stride 32 bits */
#define REG_SWPORT_DR_STRIDE	0x0c /* register stride 3*32 bits */
#define REG_SWPORT_DDR_STRIDE	0x0c /* register stride 3*32 bits */
\
    
struct DESIGNWAREGPIOBank {
    uint32_t    dr;
    uint32_t    dr_val;
    uint32_t    ddr;    /* 0=in, 1=out */
    uint32_t    last_dr_val;

    /* internal state */
    /* state from user */
    uint32_t    in_mask;
    uint32_t    in;
};

struct DESIGNWAREGPIOState {
    SysBusDevice parent_obj;
    MemoryRegion mmio;

    qemu_irq irq;
    qemu_irq output[DESIGNWARE_GPIO_PINS];

    struct DESIGNWAREGPIOBank bank[DESIGNWARE_GPIO_BANKS];

    uint32_t int_en;
    uint32_t int_mask;
    uint32_t int_level;         /* 0 = level, 1 = edge */
    uint32_t int_polarity;
    uint32_t int_status;
    uint32_t int_status_raw;
    uint32_t porta_debounce;
    /* config */
    uint32_t ngpio;
};

#endif /* DESIGNWARE_GPIO_H */
