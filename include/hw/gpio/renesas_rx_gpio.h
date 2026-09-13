/*
 * Renesas RX I/O Ports (GPIO) and Multi-Function Pin Controller (MPC)
 *
 * Datasheet: RX65N Group, RX651 Group User's Manual: Hardware
 *            (Rev.1.00 R01UH0590EJ0100), sections 20 (I/O Ports) and 22 (MPC)
 *
 * Copyright (c) 2026 Umar, Sayyad Mahammad
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 */

#ifndef HW_GPIO_RENESAS_RX_GPIO_H
#define HW_GPIO_RENESAS_RX_GPIO_H

#include "hw/core/sysbus.h"
#include "qom/object.h"

#define TYPE_RENESAS_RX_GPIO "renesas-rx-gpio"
typedef struct RenesasRxGpioState RenesasRxGpioState;
DECLARE_INSTANCE_CHECKER(RenesasRxGpioState, RENESAS_RX_GPIO,
                         TYPE_RENESAS_RX_GPIO)

/* Number of 8-bit ports modelled (PORT0..PORT1F register indices). */
#define RX_GPIO_NR_PORTS    0x20
#define RX_GPIO_NR_PINS     (RX_GPIO_NR_PORTS * 8)

/* MMIO regions. */
enum {
    RX_GPIO_MMIO_PORT = 0,  /* I/O port register block (0x0008C000) */
    RX_GPIO_MMIO_MPC,       /* MPC pin-function block   (0x0008C100) */
    RX_GPIO_NR_MMIO,
};

#define RX_GPIO_PORT_SIZE   0x100
#define RX_GPIO_MPC_SIZE    0x200

struct RenesasRxGpioState {
    /*< private >*/
    SysBusDevice parent_obj;
    /*< public >*/

    MemoryRegion port_mr;
    MemoryRegion mpc_mr;

    /* Per-port registers. */
    uint8_t pdr[RX_GPIO_NR_PORTS];      /* direction: 1 = output         */
    uint8_t podr[RX_GPIO_NR_PORTS];     /* output data                   */
    uint8_t pmr[RX_GPIO_NR_PORTS];      /* mode: 1 = peripheral          */
    uint8_t odr[RX_GPIO_NR_PORTS * 2];  /* open-drain control (2/port)   */
    uint8_t pcr[RX_GPIO_NR_PORTS];      /* pull-up control               */
    uint8_t dscr[RX_GPIO_NR_PORTS];     /* drive capacity                */

    /* External input level driven onto each port by board wiring. */
    uint8_t input[RX_GPIO_NR_PORTS];

    /* MPC PFS storage and write-protect register. */
    uint8_t mpc[RX_GPIO_MPC_SIZE];

    /* Output lines, one per pin (port * 8 + bit). */
    qemu_irq out[RX_GPIO_NR_PINS];
};

#endif /* HW_GPIO_RENESAS_RX_GPIO_H */
