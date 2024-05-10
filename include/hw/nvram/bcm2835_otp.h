/*
 * BCM2835 One-Time Programmable (OTP) Memory
 *
 * Copyright (c) 2024 Rayhan Faizel <rayhan.faizel@gmail.com>
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef BCM2835_OTP_H
#define BCM2835_OTP_H

#include "hw/sysbus.h"
#include "qom/object.h"

#define TYPE_BCM2835_OTP "bcm2835-otp"
OBJECT_DECLARE_SIMPLE_TYPE(BCM2835OTPState, BCM2835_OTP)

/* https://elinux.org/BCM2835_registers#OTP */
#define BCM2835_OTP_BOOTMODE_REG            0x00
#define BCM2835_OTP_CONFIG_REG              0x04
#define BCM2835_OTP_CTRL_LO_REG             0x08
#define BCM2835_OTP_CTRL_HI_REG             0x0c
#define BCM2835_OTP_STATUS_REG              0x10
#define BCM2835_OTP_BITSEL_REG              0x14
#define BCM2835_OTP_DATA_REG                0x18
#define BCM2835_OTP_ADDR_REG                0x1c
#define BCM2835_OTP_WRITE_DATA_READ_REG     0x20
#define BCM2835_OTP_INIT_STATUS_REG         0x24

struct BCM2835OTPState {
    /* <private> */
    SysBusDevice parent_obj;

    /* <public> */
    MemoryRegion iomem;
    uint32_t otp_rows[66];
};


uint32_t bcm2835_otp_read_row(BCM2835OTPState *s, unsigned int row);
void bcm2835_otp_write_row(BCM2835OTPState *s, unsigned row, uint32_t value);

#endif
