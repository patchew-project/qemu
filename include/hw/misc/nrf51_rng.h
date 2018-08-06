/*
 * nRF51 Random Number Generator
 *
 * Copyright 2018 Steffen Görtz <contrib@steffen-goertz.de>
 *
 * This code is licensed under the GPL version 2 or later.  See
 * the COPYING file in the top-level directory.
 *
 * See NRF51 reference manual section 21 Random Number Generator
 * See NRF51 product specification section 8.16 Random Number Generator
 *
 * QEMU interface:
 * + Property "period_unfiltered_us": Time between two biased values in
 *   microseconds.
 * + Property "period_filtered_us": Time between two unbiased values in
 *   microseconds.
 * + sysbus MMIO regions 0: Memory Region with tasks, events and registers
 *   to be mapped to the peripherals instance address by the SOC.
 * + Named GPIO output "irq": Interrupt line of the peripheral. Must be
 *   connected to the associated peripheral interrupt line of the NVIC.
 * + Named GPIO output "eep_valrdy": Event set when new random value is ready
 *   to be read.
 * + Named GPIO input "tep_start": Task that triggers start of continuous
 *   generation of random values.
 * + Named GPIO input "tep_stop": Task that ends continuous generation of
 *   random values.
 *
 * Accuracy of the peripheral model:
 * + Stochastic properties of different configurations of the random source
 *   are not modeled.
 * + Generation of unfiltered and filtered random values take at least the
 *   average generation time stated in the production specification;
 *   non-deterministic generation times are not modeled.
 *
 */
#ifndef NRF51_RNG_H
#define NRF51_RNG_H

#include "hw/sysbus.h"
#include "qemu/timer.h"
#define TYPE_NRF51_RNG "nrf51_soc.rng"
#define NRF51_RNG(obj) OBJECT_CHECK(Nrf51RNGState, (obj), TYPE_NRF51_RNG)

typedef struct Nrf51RNGState {
    SysBusDevice parent_obj;

    MemoryRegion mmio;
    qemu_irq irq;

    /* Event End Points */
    qemu_irq eep_valrdy;

    QEMUTimer timer;

    /* Time between generation of successive unfiltered values in us */
    uint16_t period_unfiltered_us;
    /* Time between generation of successive filtered values in us */
    uint16_t period_filtered_us;

    uint8_t value;

    uint32_t active;
    uint32_t event_valrdy;
    uint32_t shortcut_stop_on_valrdy;
    uint32_t interrupt_enabled;
    uint32_t filter_enabled;

} Nrf51RNGState;


#endif /* NRF51_RNG_H_ */
