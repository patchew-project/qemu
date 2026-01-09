/*
 * QEMU RISC-V Advanced Interrupt Architecture (AIA)
 *
 * Copyright (C) 2019 Western Digital Corporation or its affiliates.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef HW_RISCV_AIA_H
#define HW_RISCV_AIA_H

#include "exec/hwaddr.h"

/*
 * The virt machine physical address space used by some of the devices
 * namely ACLINT, PLIC, APLIC, and IMSIC depend on number of Sockets,
 * number of CPUs, and number of IMSIC guest files.
 *
 * Various limits defined by VIRT_SOCKETS_MAX_BITS, VIRT_CPUS_MAX_BITS,
 * and VIRT_IRQCHIP_MAX_GUESTS_BITS are tuned for maximum utilization
 * of virt machine physical address space.
 */

#define VIRT_SOCKETS_MAX_BITS          2
#define VIRT_CPUS_MAX_BITS             9
#define VIRT_CPUS_MAX                  (1 << VIRT_CPUS_MAX_BITS)
#define VIRT_SOCKETS_MAX               (1 << VIRT_SOCKETS_MAX_BITS)

#define VIRT_IRQCHIP_NUM_MSIS 255
#define VIRT_IRQCHIP_NUM_SOURCES 96
#define VIRT_IRQCHIP_NUM_PRIO_BITS 3
#define VIRT_IRQCHIP_MAX_GUESTS_BITS 3
#define VIRT_IRQCHIP_MAX_GUESTS ((1U << VIRT_IRQCHIP_MAX_GUESTS_BITS) - 1U)


#define VIRT_IMSIC_GROUP_MAX_SIZE      (1U << IMSIC_MMIO_GROUP_MIN_SHIFT)
#if VIRT_IMSIC_GROUP_MAX_SIZE < \
    IMSIC_GROUP_SIZE(VIRT_CPUS_MAX_BITS, VIRT_IRQCHIP_MAX_GUESTS_BITS)
#error "Can't accommodate single IMSIC group in address space"
#endif

#define VIRT_IMSIC_MAX_SIZE            (VIRT_SOCKETS_MAX * \
                                        VIRT_IMSIC_GROUP_MAX_SIZE)
#if 0x4000000 < VIRT_IMSIC_MAX_SIZE
#error "Can't accommodate all IMSIC groups in address space"
#endif

uint32_t imsic_num_bits(uint32_t count);

DeviceState *riscv_create_aia(bool msimode, int aia_guests,
                             uint16_t num_sources,
                             const MemMapEntry *aplic_m,
                             const MemMapEntry *aplic_s,
                             const MemMapEntry *imsic_m,
                             const MemMapEntry *imsic_s,
                             int socket, int base_hartid, int hart_count);


#endif
