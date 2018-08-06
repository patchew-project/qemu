/*
 * Nordic Semiconductor nRF51 non-volatile memory
 *
 * It provides an interface to erase regions in flash memory.
 * Furthermore it provides the user and factory information registers.
 *
 * QEMU interface:
 * + sysbus MMIO regions 0: NVMC peripheral registers
 * + sysbus MMIO regions 1: FICR peripheral registers
 * + sysbus MMIO regions 2: UICR peripheral registers
 * + page_size property to set the page size in bytes.
 * + code_size property to set the code size in number of pages.
 *
 * Accuracy of the peripheral model:
 * + The NVMC is always ready, all requested erase operations succeed
 *   immediately.
 * + CONFIG.WEN and CONFIG.EEN flags can be written and read back
 *   but are not evaluated to check whether a requested write/erase operation
 *   is legal.
 * + Code regions (MPU configuration) are disregarded.
 *
 * Copyright 2018 Steffen Görtz <contrib@steffen-goertz.de>
 *
 * This code is licensed under the GPL version 2 or later.  See
 * the COPYING file in the top-level directory.
 *
 */
#ifndef NRF51_NVM_H
#define NRF51_NVM_H

#include "hw/sysbus.h"
#define TYPE_NRF51_NVM "nrf51_soc.nvm"
#define NRF51_NVM(obj) OBJECT_CHECK(Nrf51NVMState, (obj), TYPE_NRF51_NVM)

#define NRF51_UICR_FIXTURE_SIZE 64

typedef struct Nrf51NVMState {
    SysBusDevice parent_obj;

    MemoryRegion mmio;
    MemoryRegion ficr;
    MemoryRegion uicr;

    uint32_t uicr_content[NRF51_UICR_FIXTURE_SIZE];
    uint32_t code_size;
    uint16_t page_size;
    uint8_t *empty_page;
    MemoryRegion *mr;
    AddressSpace as;

    uint32_t config;

} Nrf51NVMState;


#endif
