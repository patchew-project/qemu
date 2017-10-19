/*
 * Kinetis K64 peripheral microcontroller emulation.
 *
 * Copyright (c) 2017 Advantech Wireless
 * Written by Gabriel Costa <gabriel291075@gmail.com>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License version 2 or
 *  (at your option) any later version.
 */
 
/* Kinetis K64 series FLEXTIMER controller.  */

#ifndef KINETIS_FLEXTIMER_H
#define KINETIS_FLEXTIMER_H

#include "hw/sysbus.h"
#include "chardev/char-fe.h"
#include "chardev/char-mux.h"
#include "hw/hw.h"

#define TYPE_KINETIS_K64_FLEXTIMER "kinetis_K64_flextimer"
#define KINETIS_K64_FLEXTIMER(obj) \
    OBJECT_CHECK(kinetis_k64_flextimer_state, (obj), TYPE_KINETIS_K64_FLEXTIMER)

typedef struct {
    SysBusDevice parent_obj;

    MemoryRegion iomem;

    uint32_t SC;        /**< Status And Control, offset: 0x0 */
    uint32_t CNT;       /**< Counter, offset: 0x4 */
    uint32_t MOD;       /**< Modulo, offset: 0x8 */
    struct {            /* offset: 0xC, array step: 0x8 */
     uint32_t CnSC;     /**< Ch(n) Status And Control, offset: 0xC, step: 0x8 */
     uint32_t CnV;      /**< Ch(n) Value, array offset: 0x10, array step: 0x8 */
    }CONTROLS[8];
    uint32_t CNTIN;     /**< Counter Initial Value, offset: 0x4C */
    uint32_t STATUS;    /**< Capture And Compare Status, offset: 0x50 */
    uint32_t MODE;      /**< Features Mode Selection, offset: 0x54 */
    uint32_t SYNC;      /**< Synchronization, offset: 0x58 */
    uint32_t OUTINIT;   /**< Initial State For Channels Output, offset: 0x5C */
    uint32_t OUTMASK;   /**< Output Mask, offset: 0x60 */
    uint32_t COMBINE;   /**< Function For Linked Channels, offset: 0x64 */
    uint32_t DEADTIME;  /**< Deadtime Insertion Control, offset: 0x68 */
    uint32_t EXTTRIG;   /**< FTM External Trigger, offset: 0x6C */
    uint32_t POL;       /**< Channels Polarity, offset: 0x70 */
    uint32_t FMS;       /**< Fault Mode Status, offset: 0x74 */
    uint32_t FILTER;    /**< Input Capture Filter Control, offset: 0x78 */
    uint32_t FLTCTRL;   /**< Fault Control, offset: 0x7C */
    uint32_t QDCTRL; /**< Quadrature Decoder Control And Status, offset: 0x80 */
    uint32_t CONF;      /**< Configuration, offset: 0x84 */
    uint32_t FLTPOL;    /**< FTM Fault Input Polarity, offset: 0x88 */
    uint32_t SYNCONF;   /**< Synchronization Configuration, offset: 0x8C */
    uint32_t INVCTRL;   /**< FTM Inverting Control, offset: 0x90 */
    uint32_t SWOCTRL;   /**< FTM Software Output Control, offset: 0x94 */
    uint32_t PWMLOAD;   /**< FTM PWM Load, offset: 0x98 */    
    
    qemu_irq irq;
} kinetis_k64_flextimer_state;

#endif