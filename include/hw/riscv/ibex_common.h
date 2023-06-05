/*
 * QEMU RISC-V Helpers for LowRISC OpenTitan EarlGrey and related systems
 *
 * Copyright (c) 2022-2023 Rivos, Inc.
 *
 * Author(s):
 *  Emmanuel Blot <eblot@rivosinc.com>
 *  Loïc Lefort <loic@rivosinc.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef HW_RISCV_IBEX_COMMON_H
#define HW_RISCV_IBEX_COMMON_H

#include "qemu/osdep.h"
#include "qom/object.h"
#include "exec/hwaddr.h"
#include "hw/qdev-core.h"


/* ------------------------------------------------------------------------ */
/* Devices & GPIOs */
/* ------------------------------------------------------------------------ */

#define IBEX_MAX_MMIO_ENTRIES 4u
#define IBEX_MAX_GPIO_ENTRIES 16u

typedef struct IbexDeviceDef IbexDeviceDef;

typedef void (*ibex_dev_cfg_fn)(DeviceState *dev, const IbexDeviceDef *def,
                                DeviceState *parent);

/**
 * Structure defining a GPIO connection (in particular, IRQs) from the current
 * device to a target device
 */
typedef struct {
    /** Source GPIO */
    struct {
        /** Name of source GPIO array or NULL for unnamed */
        const char *name;
        /** Index of source output GPIO */
        int num;
    } out;

    /** Target GPIO */
    struct {
        /** Target device index */
        int index;
        /** Name of target input GPIO array or NULL for unnamed */
        const char *name;
        /** Index of target input GPIO */
        int num;
    } in;
} IbexGpioConnDef;

typedef struct {
    /** Name of the property to assign the linked device to */
    const char *propname;
    /** Linked device index */
    int index;
} IbexDeviceLinkDef;

/** Type of device property */
typedef enum {
    IBEX_PROP_TYPE_BOOL,
    IBEX_PROP_TYPE_INT,
    IBEX_PROP_TYPE_UINT,
    IBEX_PROP_TYPE_STR,
} IbexPropertyType;

typedef struct {
    /** Name of the property */
    const char *propname;
    /** Type of property */
    IbexPropertyType type;
    /** Value */
    union {
        bool b;
        int64_t i;
        uint64_t u;
        const char *s;
    };
} IbexDevicePropDef;

struct IbexDeviceDef {
    /** Registered type of the device */
    const char *type;
    /** Optional name, may be NULL */
    const char *name;
    /** Instance number, default to 0 */
    int instance;
    /** Optional configuration function */
    ibex_dev_cfg_fn cfg;
    /** Array of memory map */
    const MemMapEntry *memmap;
    /** Array of GPIO connections */
    const IbexGpioConnDef *gpio;
    /** Array of linked devices */
    const IbexDeviceLinkDef *link;
    /** Array of properties */
    const IbexDevicePropDef *prop;
};

/*
 * Special memory address marked to flag a special MemMapEntry.
 * Flagged MemMapEntry are used to select a memory region while mem mapping
 * devices. There could be up to 4 different regions.
 */
#define IBEX_MEMMAP_REGIDX_COUNT 4u
#define IBEX_MEMMAP_REGIDX_MASK \
    ((IBEX_MEMMAP_REGIDX_COUNT) - 1u) /* address are always word-aligned */
#define IBEX_MEMMAP_MAKE_REG(_addr_, _flag_) \
    ((_addr_) | ((_flag_) & IBEX_MEMMAP_REGIDX_MASK))
#define IBEX_MEMMAP_GET_REGIDX(_addr_) ((_addr_) & IBEX_MEMMAP_REGIDX_MASK)
#define IBEX_MEMMAP_GET_ADDRESS(_addr_) ((_addr_) & ~IBEX_MEMMAP_REGIDX_MASK)

/**
 * Create memory map entries, each arg is MemMapEntry definition
 */
#define MEMMAPENTRIES(...) \
    (const MemMapEntry[]) \
    { \
        __VA_ARGS__, \
        { \
            .size = 0u \
        } \
    }

/**
 * Create GPIO connection entries, each arg is IbexGpioConnDef definition
 */
#define IBEXGPIOCONNDEFS(...) \
    (const IbexGpioConnDef[]) \
    { \
        __VA_ARGS__, \
        { \
            .out = {.num = -1 } \
        } \
    }

/**
 * Create device link entries, each arg is IbexDeviceLinkDef definition
 */
#define IBEXDEVICELINKDEFS(...) \
    (const IbexDeviceLinkDef[]) \
    { \
        __VA_ARGS__, \
        { \
            .propname = NULL \
        } \
    }

/**
 * Create device property entries, each arg is IbexDevicePropDef definition
 */
#define IBEXDEVICEPROPDEFS(...) \
    (const IbexDevicePropDef[]) \
    { \
        __VA_ARGS__, \
        { \
            .propname = NULL \
        } \
    }

/**
 * Create a IbexGpioConnDef to connect two unnamed GPIOs
 */
#define IBEX_GPIO(_irq_, _in_idx_, _num_) \
    { \
        .out = { \
            .num = (_irq_), \
        }, \
        .in = { \
            .index = (_in_idx_), \
            .num = (_num_), \
        } \
    }

/**
 * Create a IbexGpioConnDef to connect a SysBus IRQ to an unnamed GPIO
 */
#define IBEX_GPIO_SYSBUS_IRQ(_irq_, _in_idx_, _num_) \
    { \
        .out = { \
            .name = SYSBUS_DEVICE_GPIO_IRQ, \
            .num = (_irq_), \
        }, \
        .in = { \
            .index = (_in_idx_), \
            .num = (_num_), \
        } \
    }

/**
 * Create a IbexLinkDeviceDef to link one device to another
 */
#define IBEX_DEVLINK(_pname_, _idx_) \
    { \
        .propname = (_pname_), .index = (_idx_), \
    }

/**
 * Create a boolean device property
 */
#define IBEX_DEV_BOOL_PROP(_pname_, _b_) \
    { \
        .propname = (_pname_), .type = IBEX_PROP_TYPE_BOOL, .b = (_b_), \
    }

/**
 * Create a signed integer device property
 */
#define IBEX_DEV_INT_PROP(_pname_, _i_) \
    { \
        .propname = (_pname_), .type = IBEX_PROP_TYPE_INT, .i = (_i_), \
    }

/**
 * Create an unsigned integer device property
 */
#define IBEX_DEV_UINT_PROP(_pname_, _u_) \
    { \
        .propname = (_pname_), .type = IBEX_PROP_TYPE_UINT, .u = (_u_), \
    }

/**
 * Create a string device property
 */
#define IBEX_DEV_STRING_PROP(_pname_, _s_) \
    { \
        .propname = (_pname_), .type = IBEX_PROP_TYPE_STR, .s = (_s_), \
    }

DeviceState **ibex_create_devices(const IbexDeviceDef *defs, unsigned count,
                                  DeviceState *parent);
void ibex_link_devices(DeviceState **devices, const IbexDeviceDef *defs,
                       unsigned count);
void ibex_define_device_props(DeviceState **devices, const IbexDeviceDef *defs,
                              unsigned count);
void ibex_realize_system_devices(DeviceState **devices,
                                 const IbexDeviceDef *defs, unsigned count);
void ibex_realize_devices(DeviceState **devices, BusState *bus,
                          const IbexDeviceDef *defs, unsigned count);
void ibex_connect_devices(DeviceState **devices, const IbexDeviceDef *defs,
                          unsigned count);
void ibex_map_devices(DeviceState **devices, MemoryRegion **mrs,
                      const IbexDeviceDef *defs, unsigned count);
void ibex_configure_devices(DeviceState **devices, BusState *bus,
                            const IbexDeviceDef *defs, unsigned count);

/**
 * Utility function to configure unimplemented device.
 * The Ibex device definition should have one defined memory entry, and an
 * optional name.
 */
void ibex_unimp_configure(DeviceState *dev, const IbexDeviceDef *def,
                          DeviceState *parent);

/* ------------------------------------------------------------------------ */
/* CPU */
/* ------------------------------------------------------------------------ */

/**
 * Load an ELF application into a CPU address space.
 * @as the address space to load the application into, maybe NULL to use the
 * default address space
 */
void ibex_load_kernel(AddressSpace *as);

/**
 * Helper for device debugging: report the current guest PC, if any.
 *
 * If a HW access is performed from another device but the CPU, reported PC
 * is 0.
 */
uint64_t ibex_get_current_pc(void);

enum {
    RV_GPR_PC = (1u << 0u),
    RV_GPR_RA = (1u << 1u),
    RV_GPR_SP = (1u << 2u),
    RV_GPR_GP = (1u << 3u),
    RV_GPR_TP = (1u << 4u),
    RV_GPR_T0 = (1u << 5u),
    RV_GPR_T1 = (1u << 6u),
    RV_GPR_T2 = (1u << 7u),
    RV_GPR_S0 = (1u << 8u),
    RV_GPR_S1 = (1u << 9u),
    RV_GPR_A0 = (1u << 10u),
    RV_GPR_A1 = (1u << 11u),
    RV_GPR_A2 = (1u << 12u),
    RV_GPR_A3 = (1u << 13u),
    RV_GPR_A4 = (1u << 14u),
    RV_GPR_A5 = (1u << 15u),
    RV_GPR_A6 = (1u << 16u),
    RV_GPR_A7 = (1u << 17u),
    RV_GPR_S2 = (1u << 18u),
    RV_GPR_S3 = (1u << 19u),
    RV_GPR_S4 = (1u << 20u),
    RV_GPR_S5 = (1u << 21u),
    RV_GPR_S6 = (1u << 22u),
    RV_GPR_S7 = (1u << 23u),
    RV_GPR_S8 = (1u << 24u),
    RV_GPR_S9 = (1u << 25u),
    RV_GPR_S10 = (1u << 26u),
    RV_GPR_S11 = (1u << 27u),
    RV_GPR_T3 = (1u << 28u),
    RV_GPR_T4 = (1u << 29u),
    RV_GPR_T5 = (1u << 30u),
    RV_GPR_T6 = (1u << 31u),
};

/**
 * Log current vCPU registers.
 *
 * @regbm is a bitmap of registers to be dumped [x1..t6], pc replace x0
 */
void ibex_log_vcpu_registers(uint64_t regbm);

#endif /* HW_RISCV_IBEX_COMMON_H */
