/*
 * QEMU LASI PS/2 emulation
 *
 * Copyright (c) 2019 Sven Schnelle
 *
 */
#ifndef HW_INPUT_LASIPS2_H
#define HW_INPUT_LASIPS2_H

#include "hw/sysbus.h"

#define TYPE_LASIPS2 "lasips2"
OBJECT_DECLARE_SIMPLE_TYPE(LASIPS2State, LASIPS2)

typedef struct LASIPS2Port {
    struct LASIPS2State *parent;
    MemoryRegion reg;
    void *dev;
    uint8_t id;
    uint8_t control;
    uint8_t buf;
    bool loopback_rbne;
    bool irq;
} LASIPS2Port;

/*
 * QEMU interface:
 *  + sysbus MMIO region 0 is the keyboard port interface
 *  + sysbus MMIO region 1 is the mouse port interface
 *  + sysbus IRQ 0 is the interrupt line shared between
 *    keyboard and mouse ports
 */
typedef struct LASIPS2State {
    /*< private >*/
    SysBusDevice parent_obj;

    /*< public >*/
    LASIPS2Port kbd;
    LASIPS2Port mouse;
    qemu_irq irq;
} LASIPS2State;

#endif /* HW_INPUT_LASIPS2_H */
