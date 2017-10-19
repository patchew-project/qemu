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
 
/* Kinetis K64 series SIM controller.  */

#ifndef KINETIS_SIM_H
#define KINETIS_SIM_H

#include "hw/sysbus.h"
#include "chardev/char-fe.h"
#include "chardev/char-mux.h"
#include "hw/hw.h"

#define TYPE_KINETIS_K64_SIM "kinetis_k64_sim"
#define KINETIS_K64_SIM(obj) \
    OBJECT_CHECK(kinetis_k64_sim_state, (obj), TYPE_KINETIS_K64_SIM)

typedef struct {
    SysBusDevice parent_obj;

    MemoryRegion iomem;

    uint32_t SOPT1;     /**< System Options Register 1, offset: 0x0 */
    uint32_t SOPT1CFG;  /**< SOPT1 Configuration Register, offset: 0x4 */
    uint32_t SOPT2;     /**< System Options Register 2, offset: 0x1004 */
    uint32_t SOPT4;     /**< System Options Register 4, offset: 0x100C */
    uint32_t SOPT5;     /**< System Options Register 5, offset: 0x1010 */
    uint32_t SOPT7;     /**< System Options Register 7, offset: 0x1018 */
    uint32_t SDID;      /**< System Device Id Register, offset: 0x1024 */
    uint32_t SCGC1;     /**< System Clock Gating Ctrl Reg 1, offset: 0x1028 */
    uint32_t SCGC2;     /**< System Clock Gating Ctrl Reg 2, offset: 0x102C */
    uint32_t SCGC3;     /**< System Clock Gating Ctrl Reg 3, offset: 0x1030 */
    uint32_t SCGC4;     /**< System Clock Gating Ctrl Reg 4, offset: 0x1034 */
    uint32_t SCGC5;     /**< System Clock Gating Ctrl Reg 5, offset: 0x1038 */
    uint32_t SCGC6;     /**< System Clock Gating Ctrl Reg 6, offset: 0x103C */
    uint32_t SCGC7;     /**< System Clock Gating Ctrl Reg 7, offset: 0x1040 */
    uint32_t CLKDIV1;   /**< System Clock Divider Register 1, offset: 0x1044 */
    uint32_t CLKDIV2;   /**< System Clock Divider Register 2, offset: 0x1048 */
    uint32_t FCFG1;     /**< Flash Configuration Register 1, offset: 0x104C */
    uint32_t FCFG2;     /**< Flash Configuration Register 2, offset: 0x1050 */
    uint32_t UIDH;      /**< Unique Id Register High, offset: 0x1054 */
    uint32_t UIDMH;     /**< Unique Id Register Mid-High, offset: 0x1058 */
    uint32_t UIDML;     /**< Unique Id Register Mid Low, offset: 0x105C */
    uint32_t UIDL;      /**< Unique Id Register Low, offset: 0x1060 */    

} kinetis_k64_sim_state;

#endif