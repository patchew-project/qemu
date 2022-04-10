/*
 * Allwinner sun4i-ss cryptographic offloader emulation
 *
 * Copyright (C) 2022 Corentin Labbe <clabbe@baylibre.com>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef HW_CRYPTO_ALLWINNER_SUN4I_SS_H
#define HW_CRYPTO_ALLWINNER_SUN4I_SS_H

#include "qom/object.h"
#include "hw/sysbus.h"

#define SS_RX_MAX     32
#define SS_RX_DEFAULT SS_RX_MAX
#define SS_TX_MAX     33

/**
 * Object model
 * @{
 */

#define TYPE_AW_SUN4I_SS "allwinner-sun4i-ss"
OBJECT_DECLARE_SIMPLE_TYPE(AwSun4iSSState, AW_SUN4I_SS)

/** @} */

/**
 * Allwinner sun4i-ss crypto object instance state
 */
struct AwSun4iSSState {
    /*< private >*/
    SysBusDevice  parent_obj;
    /*< public >*/

    /** Maps I/O registers in physical memory */
    MemoryRegion iomem;

    /** @} */
    unsigned char rx[SS_RX_MAX * 4];
    unsigned int rxc;
    unsigned char tx[SS_TX_MAX * 4];
    unsigned int txc;

    /**
     * @name Hardware Registers
     * @{
     */

    uint32_t    ctl;    /**< Control register */
    uint32_t    fcsr;   /**< FIFO control register */
    uint32_t    iv[5];  /**< IV registers */
    uint32_t    key[8]; /**< KEY registers */
    uint32_t    md[5];  /**< Message Digest registers */

    /** @} */

};

#endif /* HW_CRYPTO_ALLWINNER_SUN4I_SS_H */
