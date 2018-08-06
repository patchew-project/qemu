/*
 * nRF51 System-on-Chip general purpose input/output register definition
 *
 * Reference Manual: http://infocenter.nordicsemi.com/pdf/nRF51_RM_v3.0.pdf
 * Product Spec: http://infocenter.nordicsemi.com/pdf/nRF51822_PS_v3.1.pdf
 *
 * QEMU interface:
 * + sysbus MMIO regions 0: GPIO registers
 * + Unnamed GPIO inputs 0-31: Set tri-state input level for GPIO pin.
 *   Level -1: Externally Disconnected/Floating; Pull-up/down will be regarded
 *   Level 0: Input externally driven LOW
 *   Level 1: Input externally driven HIGH
 * + Unnamed GPIO outputs 0-31:
 *   Level -1: Disconnected/Floating
 *   Level 0: Driven LOW
 *   Level 1: Driven HIGH
 *
 * Accuracy of the peripheral model:
 * + The nRF51 GPIO output driver supports two modes, standard and high-current
 *   mode. These different drive modes are not modeled and handled the same.
 * + Pin SENSEing is not modeled/implemented.
 *
 * Copyright 2018 Steffen Görtz <contrib@steffen-goertz.de>
 *
 * This code is licensed under the GPL version 2 or later.  See
 * the COPYING file in the top-level directory.
 *
 */
#ifndef NRF51_GPIO_H
#define NRF51_GPIO_H

#include "hw/sysbus.h"
#define TYPE_NRF51_GPIO "nrf51_soc.gpio"
#define NRF51_GPIO(obj) OBJECT_CHECK(Nrf51GPIOState, (obj), TYPE_NRF51_GPIO)

#define NRF51_GPIO_PINS 32

typedef struct Nrf51GPIOState {
    SysBusDevice parent_obj;

    MemoryRegion mmio;
    qemu_irq irq;

    uint32_t out;
    uint32_t in;
    uint32_t in_mask;
    uint32_t dir;
    uint32_t cnf[NRF51_GPIO_PINS];

    uint32_t old_out;
    uint32_t old_out_connected;

    qemu_irq output[NRF51_GPIO_PINS];
} Nrf51GPIOState;


#endif
