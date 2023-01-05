/*
 * QEMU JAZZ RC4030 chipset
 *
 * Copyright (c) 2007-2013 Hervé Poussineau
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef HW_DMA_RC4030_H
#define HW_DMA_RC4030_H

#include "exec/memory.h"

/* rc4030.c */
typedef struct rc4030DMAState *rc4030_dma;
void rc4030_dma_read(void *dma, uint8_t *buf, int len);
void rc4030_dma_write(void *dma, uint8_t *buf, int len);

DeviceState *rc4030_init(rc4030_dma **dmas, IOMMUMemoryRegion **dma_mr);

#endif
