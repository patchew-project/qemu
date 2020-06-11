/*
 * Nuvoton NPCM7xx OTP (Fuse Array) Interface
 *
 * Copyright 2020 Google LLC
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 */
#ifndef NPCM7XX_OTP_H
#define NPCM7XX_OTP_H

#include "exec/memory.h"
#include "hw/sysbus.h"

/* Each OTP module holds 8192 bits of one-time programmable storage */
#define NPCM7XX_OTP_ARRAY_BITS (8192)
#define NPCM7XX_OTP_ARRAY_BYTES (NPCM7XX_OTP_ARRAY_BITS / 8)

/**
 * enum NPCM7xxOTPRegister - 32-bit register indices.
 */
typedef enum NPCM7xxOTPRegister {
    NPCM7XX_OTP_FST,
    NPCM7XX_OTP_FADDR,
    NPCM7XX_OTP_FDATA,
    NPCM7XX_OTP_FCFG,
    /* Offset 0x10 is FKEYIND in OTP1, FUSTRAP in OTP2 */
    NPCM7XX_OTP_FKEYIND = 0x0010 / sizeof(uint32_t),
    NPCM7XX_OTP_FUSTRAP = 0x0010 / sizeof(uint32_t),
    NPCM7XX_OTP_FCTL,
    NPCM7XX_OTP_NR_REGS,
} NPCM7xxOTPRegister;

/**
 * struct NPCM7xxOTPState - Device state for one OTP module.
 * @parent: System bus device.
 * @mmio: Memory region through which registers are accessed.
 * @regs: Register contents.
 * @array: OTP storage array.
 */
typedef struct NPCM7xxOTPState {
    SysBusDevice parent;

    MemoryRegion mmio;
    uint32_t regs[NPCM7XX_OTP_NR_REGS];
    uint8_t *array;
} NPCM7xxOTPState;

#define TYPE_NPCM7XX_OTP "npcm7xx-otp"
#define NPCM7XX_OTP(obj) OBJECT_CHECK(NPCM7xxOTPState, (obj), TYPE_NPCM7XX_OTP)

#define TYPE_NPCM7XX_KEY_STORAGE "npcm7xx-key-storage"
#define TYPE_NPCM7XX_FUSE_ARRAY "npcm7xx-fuse-array"

/**
 * struct NPCM7xxOTPClass - OTP module class.
 * @parent: System bus device class.
 * @mmio_ops: MMIO register operations for this type of module.
 *
 * The two OTP modules (key-storage and fuse-array) have slightly different
 * behavior, so we give them different MMIO register operations.
 */
typedef struct NPCM7xxOTPClass {
    SysBusDeviceClass parent;

    const MemoryRegionOps *mmio_ops;
} NPCM7xxOTPClass;

#define NPCM7XX_OTP_CLASS(klass) \
    OBJECT_CLASS_CHECK(NPCM7xxOTPClass, (klass), TYPE_NPCM7XX_OTP)
#define NPCM7XX_OTP_GET_CLASS(obj) \
    OBJECT_GET_CLASS(NPCM7xxOTPClass, (obj), TYPE_NPCM7XX_OTP)

/**
 * npcm7xx_otp_array_write - ECC encode and write data to OTP array.
 * @s: OTP module.
 * @data: Data to be encoded and written.
 * @offset: Offset of first byte to be written in the OTP array.
 * @len: Number of bytes before ECC encoding.
 *
 * Each nibble of data is encoded into a byte, so the number of bytes written
 * to the array will be @len * 2.
 */
extern void npcm7xx_otp_array_write(NPCM7xxOTPState *s, const void *data,
                                    unsigned int offset, unsigned int len);

#endif /* NPCM7XX_OTP_H */
