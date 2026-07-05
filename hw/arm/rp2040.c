/*
 * RP2040 SoC emulation
 *
 * Copyright (c) 2021 Linaro Ltd
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "qemu/units.h"
#include "qapi/error.h"
#include "hw/arm/rp2040.h"
#include "hw/core/qdev-clock.h"
#include "hw/core/qdev-properties.h"
#include "hw/core/loader.h"
#include "hw/misc/rp2040_nyi.h"
#include "hw/misc/unimp.h"
#include "fpu/softfloat.h"
#include "qemu/datadir.h"
#include "qemu/log.h"
#include "system/runstate.h"
#include "target/arm/cpu.h"
#include "target/arm/cpu-qom.h"
#include "trace.h"

#include <math.h>

#define RP2040_UART0_BASE 0x40034000
#define RP2040_UART0_IRQ  20
#define RP2040_UART1_BASE 0x40038000
#define RP2040_UART1_IRQ  21
#define RP2040_DMA_IRQ_0  11
#define RP2040_DMA_IRQ_1  12
#define RP2040_IO_IRQ_BANK0 13
#define RP2040_SIO_IRQ_PROC0 15
#define RP2040_SIO_IRQ_PROC1 16
#define RP2040_PROC1       1

#define RP2040_BOOTROM_FUNC_TABLE_PTR_OFFSET 0x14
#define RP2040_BOOTROM_DATA_TABLE_PTR_OFFSET 0x16
#define RP2040_BOOTROM_TABLE_LOOKUP_PTR_OFFSET 0x18
#define RP2040_BOOTROM_LOOKUP_OFFSET 0x0100
#define RP2040_BOOTROM_STUBS_OFFSET 0x0120
#define RP2040_BOOTROM_FUNC_TABLE_OFFSET 0x0300
#define RP2040_BOOTROM_DATA_TABLE_OFFSET 0x0340
#define RP2040_BOOTROM_FLOAT_NYI_STUB_OFFSET 0x0360
#define RP2040_BOOTROM_DOUBLE_NYI_STUB_OFFSET 0x0380
#define RP2040_BOOTROM_FLOAT_TABLE_OFFSET 0x03c0
#define RP2040_BOOTROM_DOUBLE_TABLE_OFFSET 0x0460
#define RP2040_BOOTROM_FLOAT_STUBS_OFFSET 0x0600
#define RP2040_BOOTROM_DOUBLE_STUBS_OFFSET 0x0b00
#define RP2040_BOOTROM_FP_STUB_SIZE 36
#define RP2040_BOOTROM_HARDFAULT_EXIT_OFFSET 0x0f80
#define RP2040_BOOTROM_FUNC_TABLE_ENTRY_SIZE 4
#define RP2040_BOOTROM_DATA_TABLE_ENTRY_SIZE 4
#define RP2040_BOOTROM_NYI_CODE_LITERAL_OFFSET 20
#define RP2040_BOOTROM_HELPER_NOARG_CODE_LITERAL_OFFSET 8
#define RP2040_BOOTROM_HELPER_ARGS4_CODE_LITERAL_OFFSET 24
#define RP2040_BOOTROM_HELPER_ARGS3_CODE_LITERAL_OFFSET 20
#define RP2040_BOOTROM_FP_STUB_CODE_LITERAL_OFFSET 32
#define RP2040_BOOTROM_ROM_VERSION_OFFSET 0x13
#define RP2040_BOOTROM_SYNTHETIC_ROM_VERSION 2
#define RP2040_BOOTROM_FLOAT_TABLE_WORDS 32

#define RP2040_SYNTHETIC_ROM_DBG_CMD  0x00
#define RP2040_SYNTHETIC_ROM_DBG_ARG0 0x04
#define RP2040_SYNTHETIC_ROM_DBG_ARG1 0x08
#define RP2040_SYNTHETIC_ROM_DBG_ARG2 0x0c
#define RP2040_SYNTHETIC_ROM_DBG_ARG3 0x10
#define RP2040_SYNTHETIC_ROM_DBG_RESULT0 0x14
#define RP2040_SYNTHETIC_ROM_DBG_RESULT1 0x18
#define RP2040_SYNTHETIC_ROM_DBG_RESULT2 0x1c
#define RP2040_SYNTHETIC_ROM_DBG_RESULT3 0x20
#define RP2040_SYNTHETIC_ROM_DBG_FLASH_COUNT0 0x40

#define RP2040_SYNTHETIC_ROM_DBG_CMD_EXIT 0x54495845 /* "EXIT" */

#define RP2040_SYNTHETIC_FP_CMD_MASK   0xffffff00
#define RP2040_SYNTHETIC_FP_CMD_FLOAT  0x80000000
#define RP2040_SYNTHETIC_FP_CMD_DOUBLE 0x80000100

enum RP2040SyntheticFlashHelper {
    RP2040_SYNTHETIC_FLASH_CONNECT_INTERNAL_FLASH,
    RP2040_SYNTHETIC_FLASH_EXIT_XIP,
    RP2040_SYNTHETIC_FLASH_FLUSH_CACHE,
    RP2040_SYNTHETIC_FLASH_ENTER_CMD_XIP,
    RP2040_SYNTHETIC_FLASH_RANGE_ERASE,
    RP2040_SYNTHETIC_FLASH_RANGE_PROGRAM,
};

#define RP2040_SF_TABLE_FADD            0x00
#define RP2040_SF_TABLE_FSUB            0x04
#define RP2040_SF_TABLE_FMUL            0x08
#define RP2040_SF_TABLE_FDIV            0x0c
#define RP2040_SF_TABLE_FCMP_FAST       0x10
#define RP2040_SF_TABLE_FCMP_FAST_FLAGS 0x14
#define RP2040_SF_TABLE_FSQRT           0x18
#define RP2040_SF_TABLE_FLOAT2INT       0x1c
#define RP2040_SF_TABLE_FLOAT2FIX       0x20
#define RP2040_SF_TABLE_FLOAT2UINT      0x24
#define RP2040_SF_TABLE_FLOAT2UFIX      0x28
#define RP2040_SF_TABLE_INT2FLOAT       0x2c
#define RP2040_SF_TABLE_FIX2FLOAT       0x30
#define RP2040_SF_TABLE_UINT2FLOAT      0x34
#define RP2040_SF_TABLE_UFIX2FLOAT      0x38
#define RP2040_SF_TABLE_FCOS            0x3c
#define RP2040_SF_TABLE_FSIN            0x40
#define RP2040_SF_TABLE_FTAN            0x44
#define RP2040_SF_TABLE_V3_FSINCOS      0x48
#define RP2040_SF_TABLE_FEXP            0x4c
#define RP2040_SF_TABLE_FLN             0x50
#define RP2040_SF_TABLE_FCMP_BASIC      0x54
#define RP2040_SF_TABLE_FATAN2          0x58
#define RP2040_SF_TABLE_INT642FLOAT     0x5c
#define RP2040_SF_TABLE_FIX642FLOAT     0x60
#define RP2040_SF_TABLE_UINT642FLOAT    0x64
#define RP2040_SF_TABLE_UFIX642FLOAT    0x68
#define RP2040_SF_TABLE_FLOAT2INT64     0x6c
#define RP2040_SF_TABLE_FLOAT2FIX64     0x70
#define RP2040_SF_TABLE_FLOAT2UINT64    0x74
#define RP2040_SF_TABLE_FLOAT2UFIX64    0x78
#define RP2040_SF_TABLE_FLOAT2DOUBLE    0x7c

#define USBCTRL_ADDR_ENDP       0x00
#define USBCTRL_SIE_CTRL        0x4c
#define USBCTRL_SIE_STATUS      0x50
#define USBCTRL_INT_EP_CTRL     0x54
#define USBCTRL_BUFF_STATUS     0x58
#define USBCTRL_BUFF_CPU_HANDLE 0x5c
#define USBCTRL_EP_ABORT        0x60
#define USBCTRL_EP_ABORT_DONE   0x64
#define USBCTRL_EP_STALL_ARM    0x68
#define USBCTRL_NAK_POLL        0x6c
#define USBCTRL_EP_STATUS       0x70
#define USBCTRL_USB_MUXING      0x74
#define USBCTRL_USB_PWR         0x78
#define USBCTRL_USBPHY_DIRECT   0x7c
#define USBCTRL_USBPHY_TRIM     0x80
#define USBCTRL_INTR            0x8c
#define USBCTRL_INTE            0x90
#define USBCTRL_INTF            0x94
#define USBCTRL_INTS            0x98

#define USBCTRL_SIE_STATUS_VBUS_DETECTED BIT(11)

#define ATOMIC_ALIAS_MASK 0x3000
#define ATOMIC_XOR        0x1000
#define ATOMIC_SET        0x2000
#define ATOMIC_CLR        0x3000

/*
 * Temporary boot ROM used until a faithful RP2040 boot ROM is requested.  It
 * uses SIO_CPUID to split core behavior: core 0 copies the 256-byte XIP
 * second-stage boot code into SRAM and branches to the SRAM copy; core 1 waits
 * in ROM for the Pico SDK launch FIFO sequence, echoes the received words,
 * installs VTOR/MSP, then branches to the received entry point.  Real RP2040
 * mask ROM performs more checks, but boot2 expects to run from SRAM while it
 * configures XIP.
 */
static const uint8_t rp2040_bootrom[] = {
    0x00, 0x20, 0x04, 0x20, /* initial SP: 0x20042000 */
    0x41, 0x00, 0x00, 0x00, /* reset handler: 0x00000041 */
    0xb3, 0x00, 0x00, 0x00, /* NMI handler: 0x000000b3 */
    0xb3, 0x00, 0x00, 0x00, /* HardFault handler: 0x000000b3 */
    0xb3, 0x00, 0x00, 0x00, /* reserved */
    0xb3, 0x00, 0x00, 0x00, /* reserved */
    0xb3, 0x00, 0x00, 0x00, /* reserved */
    0x00, 0x00, 0x00, 0x00, /* reserved */
    0x00, 0x00, 0x00, 0x00, /* reserved */
    0x00, 0x00, 0x00, 0x00, /* reserved */
    0x00, 0x00, 0x00, 0x00, /* reserved */
    0xb3, 0x00, 0x00, 0x00, /* SVC handler: 0x000000b3 */
    0x00, 0x00, 0x00, 0x00, /* reserved */
    0x00, 0x00, 0x00, 0x00, /* reserved */
    0xb3, 0x00, 0x00, 0x00, /* PendSV handler: 0x000000b3 */
    0xb3, 0x00, 0x00, 0x00, /* SysTick handler: 0x000000b3 */
    0x26, 0x4c,             /* ldr r4, [pc, #152] ; SIO_BASE */
    0x20, 0x68,             /* ldr r0, [r4] ; SIO_CPUID */
    0x00, 0x28,             /* cmp r0, #0 */
    0x13, 0xd1,             /* bne core1 path */
    0x25, 0x48,             /* ldr r0, [pc, #148] ; 0x10000000 */
    0x26, 0x49,             /* ldr r1, [pc, #152] ; 0x20041f00 */
    0x40, 0x22,             /* movs r2, #64 */
    0x03, 0x68,             /* ldr r3, [r0] */
    0x0b, 0x60,             /* str r3, [r1] */
    0x04, 0x30,             /* adds r0, #4 */
    0x04, 0x31,             /* adds r1, #4 */
    0x01, 0x3a,             /* subs r2, #1 */
    0xf9, 0xd1,             /* bne copy loop */
    0x23, 0x4b,             /* ldr r3, [pc, #140] ; launch entry */
    0x9e, 0x46,             /* mov lr, r3 */
    0x23, 0x48,             /* ldr r0, [pc, #140] ; 0x20041f01 */
    0x00, 0x47,             /* bx r0 */
    0x23, 0x48,             /* ldr r0, [pc, #140] ; 0x10000100 */
    0x23, 0x49,             /* ldr r1, [pc, #140] ; VTOR */
    0x08, 0x60,             /* str r0, [r1] */
    0x06, 0xc8,             /* ldm r0!, {r1, r2} */
    0x81, 0xf3, 0x08, 0x88, /* msr msp, r1 */
    0x10, 0x47,             /* bx r2 */
    0x21, 0x4d,             /* ldr r5, [pc, #132] ; sequence */
    0x00, 0x26,             /* movs r6, #0 */
    0x00, 0xf0, 0x1e, 0xf8, /* bl fifo_pop */
    0x03, 0x2e,             /* cmp r6, #3 */
    0x07, 0xd2,             /* bhs echo */
    0xb1, 0x00,             /* lsls r1, r6, #2 */
    0x6a, 0x58,             /* ldr r2, [r5, r1] */
    0x90, 0x42,             /* cmp r0, r2 */
    0x0c, 0xd0,             /* beq echo */
    0x00, 0x26,             /* movs r6, #0 */
    0x00, 0xf0, 0x1c, 0xf8, /* bl fifo_push */
    0xf3, 0xe7,             /* b core1 loop */
    0x03, 0x2e,             /* cmp r6, #3 */
    0x01, 0xd1,             /* bne maybe SP */
    0x07, 0x46,             /* mov r7, r0 */
    0x04, 0xe0,             /* b echo */
    0x04, 0x2e,             /* cmp r6, #4 */
    0x01, 0xd1,             /* bne save PC */
    0x03, 0x46,             /* mov r3, r0 */
    0x00, 0xe0,             /* b echo */
    0x05, 0x46,             /* mov r5, r0 */
    0x00, 0xf0, 0x10, 0xf8, /* bl fifo_push */
    0x01, 0x36,             /* adds r6, #1 */
    0x06, 0x2e,             /* cmp r6, #6 */
    0xe5, 0xd1,             /* bne core1 loop */
    0x12, 0x49,             /* ldr r1, [pc, #72] ; VTOR */
    0x0f, 0x60,             /* str r7, [r1] */
    0x83, 0xf3, 0x08, 0x88, /* msr msp, r3 */
    0x28, 0x47,             /* bx r5 */
    0xfe, 0xe7,             /* hang */
    0x09, 0x4c,             /* ldr r4, [pc, #36] ; SIO_BASE */
    0x20, 0x6d,             /* ldr r0, [r4, #0x50] */
    0x01, 0x21,             /* movs r1, #1 */
    0x08, 0x42,             /* tst r0, r1 */
    0xfb, 0xd0,             /* beq fifo_pop */
    0xa0, 0x6d,             /* ldr r0, [r4, #0x58] */
    0x70, 0x47,             /* bx lr */
    0x06, 0x4c,             /* ldr r4, [pc, #24] ; SIO_BASE */
    0x21, 0x6d,             /* ldr r1, [r4, #0x50] */
    0x02, 0x22,             /* movs r2, #2 */
    0x11, 0x42,             /* tst r1, r2 */
    0xfb, 0xd0,             /* beq fifo_push */
    0x60, 0x65,             /* str r0, [r4, #0x54] */
    0x70, 0x47,             /* bx lr */
    0x00, 0x00, 0x00, 0x00, /* core1 sequence[0] */
    0x00, 0x00, 0x00, 0x00, /* core1 sequence[1] */
    0x01, 0x00, 0x00, 0x00, /* core1 sequence[2] */
    0x00, 0x00, 0x00, 0xd0, /* SIO_BASE */
    0x00, 0x00, 0x00, 0x10, /* boot2 source: 0x10000000 */
    0x00, 0x1f, 0x04, 0x20, /* boot2 SRAM copy: 0x20041f00 */
    0x63, 0x00, 0x00, 0x00, /* post-boot2 launch entry: 0x00000063 */
    0x01, 0x1f, 0x04, 0x20, /* boot2 SRAM entry: 0x20041f01 */
    0x00, 0x01, 0x00, 0x10, /* application vectors: 0x10000100 */
    0x08, 0xed, 0x00, 0xe0, /* VTOR: 0xe000ed08 */
    0xd0, 0x00, 0x00, 0x00, /* core1 sequence table */
};

static const struct {
    const char *name;
    hwaddr base;
    hwaddr size;
} rp2040_unimplemented[] = {
    { "rp2040.spi0",     0x4003c000, 0x4000 },
    { "rp2040.spi1",     0x40040000, 0x4000 },
    { "rp2040.i2c0",     0x40044000, 0x4000 },
    { "rp2040.i2c1",     0x40048000, 0x4000 },
    { "rp2040.adc",      0x4004c000, 0x4000 },
    { "rp2040.pwm",      0x40050000, 0x4000 },
    { "rp2040.rtc",      0x4005c000, 0x4000 },
    { "rp2040.pio0",     0x50200000, 0x10000 },
    { "rp2040.pio1",     0x50300000, 0x10000 },
};

typedef struct RP2040BootromFunction {
    uint16_t code;
    const char *name;
    const uint8_t *impl;
    size_t impl_size;
    uint32_t code_literal_offset;
} RP2040BootromFunction;

typedef struct RP2040BootromData {
    uint16_t code;
    uint16_t offset;
} RP2040BootromData;

#define RP2040_ROM_TABLE_CODE(c1, c2) ((uint16_t)(c1) | ((uint16_t)(c2) << 8))

static const uint8_t rp2040_bootrom_clz32[] = {
    0x00, 0x21, 0x00, 0x28, 0x01, 0xd1, 0x20, 0x20,
    0x70, 0x47, 0x00, 0x28, 0x02, 0xd4, 0x40, 0x00,
    0x01, 0x31, 0xfa, 0xe7, 0x08, 0x46, 0x70, 0x47,
};

static const uint8_t rp2040_bootrom_ctz32[] = {
    0x00, 0x21, 0x00, 0x28, 0x01, 0xd1, 0x20, 0x20,
    0x70, 0x47, 0x01, 0x22, 0x10, 0x42, 0x02, 0xd1,
    0x40, 0x08, 0x01, 0x31, 0xf9, 0xe7, 0x08, 0x46,
    0x70, 0x47,
};

static const uint8_t rp2040_bootrom_popcount32[] = {
    0x00, 0x21, 0x00, 0x28, 0x04, 0xd0, 0x01, 0x22,
    0x02, 0x40, 0x89, 0x18, 0x40, 0x08, 0xf8, 0xe7,
    0x08, 0x46, 0x70, 0x47,
};

static const uint8_t rp2040_bootrom_reverse32[] = {
    0x00, 0x21, 0x20, 0x22, 0x49, 0x00, 0x01, 0x23,
    0x03, 0x40, 0x19, 0x43, 0x40, 0x08, 0x01, 0x3a,
    0xf8, 0xd1, 0x08, 0x46, 0x70, 0x47,
};

static const uint8_t rp2040_bootrom_memcpy[] = {
    0x10, 0xb4, 0x03, 0x46, 0x00, 0x2a, 0x05, 0xd0,
    0x0c, 0x78, 0x04, 0x70, 0x01, 0x31, 0x01, 0x30,
    0x01, 0x3a, 0xf7, 0xe7, 0x18, 0x46, 0x10, 0xbc,
    0x70, 0x47,
};

static const uint8_t rp2040_bootrom_memset[] = {
    0x03, 0x46, 0x00, 0x2a, 0x03, 0xd0, 0x01, 0x70,
    0x01, 0x30, 0x01, 0x3a, 0xf9, 0xe7, 0x18, 0x46,
    0x70, 0x47,
};

static const uint8_t rp2040_bootrom_memcpy44[] = {
    0x10, 0xb4, 0x03, 0x46, 0x00, 0x2a, 0x05, 0xd0,
    0x0c, 0x68, 0x04, 0x60, 0x04, 0x31, 0x04, 0x30,
    0x01, 0x3a, 0xf7, 0xe7, 0x18, 0x46, 0x10, 0xbc,
    0x70, 0x47,
};

static const uint8_t rp2040_bootrom_memset4[] = {
    0x03, 0x46, 0x00, 0x2a, 0x03, 0xd0, 0x01, 0x60,
    0x04, 0x30, 0x01, 0x3a, 0xf9, 0xe7, 0x18, 0x46,
    0x70, 0x47,
};

static const uint8_t rp2040_bootrom_flash_noarg[] = {
    0x01, 0x49,             /* ldr r1, [pc, #4] ; function code */
    0x02, 0x48,             /* ldr r0, [pc, #8] ; debug base */
    0x01, 0x60,             /* str r1, [r0] */
    0x70, 0x47,             /* bx lr */
    0x00, 0x00, 0x00, 0x00, /* function code literal */
    0x00, 0x00, 0xff, 0x5f, /* 0x5fff0000 */
};

static const uint8_t rp2040_bootrom_flash_args4[] = {
    0x10, 0xb5,             /* push {r4, lr} */
    0x04, 0x4c,             /* ldr r4, [pc, #16] ; debug base */
    0x60, 0x60,             /* str r0, [r4, #4] */
    0xa1, 0x60,             /* str r1, [r4, #8] */
    0xe2, 0x60,             /* str r2, [r4, #12] */
    0x23, 0x61,             /* str r3, [r4, #16] */
    0x02, 0x48,             /* ldr r0, [pc, #8] ; function code */
    0x20, 0x60,             /* str r0, [r4] */
    0x10, 0xbd,             /* pop {r4, pc} */
    0xc0, 0x46,             /* nop; align literal */
    0x00, 0x00, 0xff, 0x5f, /* 0x5fff0000 */
    0x00, 0x00, 0x00, 0x00, /* function code literal */
};

static const uint8_t rp2040_bootrom_flash_args3[] = {
    0x10, 0xb5,             /* push {r4, lr} */
    0x03, 0x4c,             /* ldr r4, [pc, #12] ; debug base */
    0x60, 0x60,             /* str r0, [r4, #4] */
    0xa1, 0x60,             /* str r1, [r4, #8] */
    0xe2, 0x60,             /* str r2, [r4, #12] */
    0x02, 0x48,             /* ldr r0, [pc, #8] ; function code */
    0x20, 0x60,             /* str r0, [r4] */
    0x10, 0xbd,             /* pop {r4, pc} */
    0x00, 0x00, 0xff, 0x5f, /* 0x5fff0000 */
    0x00, 0x00, 0x00, 0x00, /* function code literal */
};

static const uint8_t rp2040_bootrom_fp_stub[] = {
    0x10, 0xb5,             /* push {r4, lr} */
    0x06, 0x4c,             /* ldr r4, [pc, #24] ; debug base */
    0x60, 0x60,             /* str r0, [r4, #4] */
    0xa1, 0x60,             /* str r1, [r4, #8] */
    0xe2, 0x60,             /* str r2, [r4, #12] */
    0x23, 0x61,             /* str r3, [r4, #16] */
    0x04, 0x48,             /* ldr r0, [pc, #16] ; command */
    0x20, 0x60,             /* str r0, [r4] */
    0x60, 0x69,             /* ldr r0, [r4, #20] */
    0xa1, 0x69,             /* ldr r1, [r4, #24] */
    0xe2, 0x69,             /* ldr r2, [r4, #28] */
    0x23, 0x6a,             /* ldr r3, [r4, #32] */
    0x10, 0xbd,             /* pop {r4, pc} */
    0xc0, 0x46,             /* nop; align literal */
    0x00, 0x00, 0xff, 0x5f, /* 0x5fff0000 */
    0x00, 0x00, 0x00, 0x00, /* command literal */
};

#define RP2040_BOOTROM_IMPL(_code, _name, _impl) \
    { _code, _name, _impl, sizeof(_impl), UINT32_MAX }
#define RP2040_BOOTROM_IMPL_CODE(_code, _name, _impl, _offset) \
    { _code, _name, _impl, sizeof(_impl), _offset }
#define RP2040_BOOTROM_NYI(_code, _name) \
    { _code, _name, NULL, 0, UINT32_MAX }

static const RP2040BootromFunction rp2040_bootrom_functions[] = {
    RP2040_BOOTROM_IMPL_CODE(RP2040_ROM_TABLE_CODE('C', 'X'),
                             "flash_enter_cmd_xip",
                             rp2040_bootrom_flash_noarg,
                             RP2040_BOOTROM_HELPER_NOARG_CODE_LITERAL_OFFSET),
    RP2040_BOOTROM_IMPL_CODE(RP2040_ROM_TABLE_CODE('E', 'X'),
                             "flash_exit_xip",
                             rp2040_bootrom_flash_noarg,
                             RP2040_BOOTROM_HELPER_NOARG_CODE_LITERAL_OFFSET),
    RP2040_BOOTROM_IMPL_CODE(RP2040_ROM_TABLE_CODE('F', 'C'),
                             "flash_flush_cache",
                             rp2040_bootrom_flash_noarg,
                             RP2040_BOOTROM_HELPER_NOARG_CODE_LITERAL_OFFSET),
    RP2040_BOOTROM_IMPL_CODE(RP2040_ROM_TABLE_CODE('I', 'F'),
                             "connect_internal_flash",
                             rp2040_bootrom_flash_noarg,
                             RP2040_BOOTROM_HELPER_NOARG_CODE_LITERAL_OFFSET),
    RP2040_BOOTROM_IMPL_CODE(RP2040_ROM_TABLE_CODE('R', 'E'),
                             "flash_range_erase",
                             rp2040_bootrom_flash_args4,
                             RP2040_BOOTROM_HELPER_ARGS4_CODE_LITERAL_OFFSET),
    RP2040_BOOTROM_IMPL_CODE(RP2040_ROM_TABLE_CODE('R', 'P'),
                             "flash_range_program",
                             rp2040_bootrom_flash_args3,
                             RP2040_BOOTROM_HELPER_ARGS3_CODE_LITERAL_OFFSET),
    RP2040_BOOTROM_IMPL(RP2040_ROM_TABLE_CODE('C', '4'), "memcpy44",
                        rp2040_bootrom_memcpy44),
    RP2040_BOOTROM_IMPL(RP2040_ROM_TABLE_CODE('L', '3'), "clz32",
                        rp2040_bootrom_clz32),
    RP2040_BOOTROM_IMPL(RP2040_ROM_TABLE_CODE('M', 'C'), "memcpy",
                        rp2040_bootrom_memcpy),
    RP2040_BOOTROM_IMPL(RP2040_ROM_TABLE_CODE('M', 'S'), "memset",
                        rp2040_bootrom_memset),
    RP2040_BOOTROM_IMPL(RP2040_ROM_TABLE_CODE('P', '3'), "popcount32",
                        rp2040_bootrom_popcount32),
    RP2040_BOOTROM_IMPL(RP2040_ROM_TABLE_CODE('R', '3'), "reverse32",
                        rp2040_bootrom_reverse32),
    RP2040_BOOTROM_IMPL(RP2040_ROM_TABLE_CODE('S', '4'), "memset4",
                        rp2040_bootrom_memset4),
    RP2040_BOOTROM_IMPL(RP2040_ROM_TABLE_CODE('T', '3'), "ctz32",
                        rp2040_bootrom_ctz32),
    RP2040_BOOTROM_NYI(RP2040_ROM_TABLE_CODE('U', 'B'), "reset_usb_boot"),
};

static const RP2040BootromData rp2040_bootrom_data[] = {
    { RP2040_ROM_TABLE_CODE('S', 'F'), RP2040_BOOTROM_FLOAT_TABLE_OFFSET + 2 },
    { RP2040_ROM_TABLE_CODE('S', 'D'), RP2040_BOOTROM_DOUBLE_TABLE_OFFSET + 2 },
};

static const uint8_t rp2040_bootrom_lookup[] = {
    0x02, 0x88,             /* ldrh r2, [r0] */
    0x00, 0x2a,             /* cmp r2, #0 */
    0x05, 0xd0,             /* beq not_found */
    0x91, 0x42,             /* cmp r1, r2 */
    0x01, 0xd0,             /* beq found */
    0x04, 0x30,             /* adds r0, #4 */
    0xf8, 0xe7,             /* b loop */
    0x40, 0x88,             /* found: ldrh r0, [r0, #2] */
    0x70, 0x47,             /* bx lr */
    0x5f, 0x22,             /* not_found: movs r2, #0x5f */
    0x12, 0x06,             /* lsls r2, r2, #24 */
    0xff, 0x23,             /* movs r3, #0xff */
    0x1b, 0x04,             /* lsls r3, r3, #16 */
    0x1a, 0x43,             /* orrs r2, r3 ; 0x5fff0000 */
    0x11, 0x60,             /* str r1, [r2] */
    0x00, 0xbe,             /* bkpt #0 */
    0x00, 0x20,             /* movs r0, #0 */
    0x70, 0x47,             /* bx lr */
};

static const uint8_t rp2040_bootrom_nyi_stub[] = {
    0x04, 0x49,             /* ldr r1, [pc, #16] ; function code */
    0x5f, 0x20,             /* movs r0, #0x5f */
    0x00, 0x06,             /* lsls r0, r0, #24 */
    0xff, 0x22,             /* movs r2, #0xff */
    0x12, 0x04,             /* lsls r2, r2, #16 */
    0x10, 0x43,             /* orrs r0, r2 ; 0x5fff0000 */
    0x01, 0x60,             /* str r1, [r0] */
    0x00, 0xbe,             /* bkpt #0 */
    0xfe, 0xe7,             /* b . */
    0xc0, 0x46,             /* nop; align literal */
    0x00, 0x00, 0x00, 0x00, /* function code literal */
};

static const uint8_t rp2040_bootrom_hardfault_exit[] = {
    0xef, 0xf3, 0x08, 0x81, /* mrs r1, msp */
    0x8a, 0x69,             /* ldr r2, [r1, #24] ; stacked PC */
    0x13, 0x88,             /* ldrh r3, [r2] */
    0x04, 0x4c,             /* ldr r4, [pc, #16] ; 0xbe00 */
    0xa3, 0x42,             /* cmp r3, r4 */
    0x04, 0xd1,             /* bne hang */
    0x08, 0x68,             /* ldr r0, [r1] ; stacked R0/status */
    0x03, 0x4c,             /* ldr r4, [pc, #12] ; debug base */
    0x60, 0x60,             /* str r0, [r4, #4] */
    0x03, 0x48,             /* ldr r0, [pc, #12] ; EXIT command */
    0x20, 0x60,             /* str r0, [r4] */
    0xfe, 0xe7,             /* hang: b hang */
    0x00, 0x00,             /* align literal pool */
    0x00, 0xbe, 0x00, 0x00, /* 0xbe00 */
    0x00, 0x00, 0xff, 0x5f, /* 0x5fff0000 */
    0x45, 0x58, 0x49, 0x54, /* "EXIT" */
};

static void rp2040_store_hword(uint8_t *rom, uint32_t offset, uint16_t value)
{
    rom[offset] = value;
    rom[offset + 1] = value >> 8;
}

static void rp2040_store_word(uint8_t *rom, uint32_t offset, uint32_t value)
{
    rom[offset] = value;
    rom[offset + 1] = value >> 8;
    rom[offset + 2] = value >> 16;
    rom[offset + 3] = value >> 24;
}

static bool rp2040_bootrom_fp_supported(uint32_t offset)
{
    switch (offset) {
    case RP2040_SF_TABLE_FADD:
    case RP2040_SF_TABLE_FSUB:
    case RP2040_SF_TABLE_FMUL:
    case RP2040_SF_TABLE_FDIV:
    case RP2040_SF_TABLE_FCMP_FAST:
    case RP2040_SF_TABLE_FCMP_FAST_FLAGS:
    case RP2040_SF_TABLE_FSQRT:
    case RP2040_SF_TABLE_FLOAT2INT:
    case RP2040_SF_TABLE_FLOAT2FIX:
    case RP2040_SF_TABLE_FLOAT2UINT:
    case RP2040_SF_TABLE_FLOAT2UFIX:
    case RP2040_SF_TABLE_INT2FLOAT:
    case RP2040_SF_TABLE_FIX2FLOAT:
    case RP2040_SF_TABLE_UINT2FLOAT:
    case RP2040_SF_TABLE_UFIX2FLOAT:
    case RP2040_SF_TABLE_FCOS:
    case RP2040_SF_TABLE_FSIN:
    case RP2040_SF_TABLE_FTAN:
    case RP2040_SF_TABLE_V3_FSINCOS:
    case RP2040_SF_TABLE_FEXP:
    case RP2040_SF_TABLE_FLN:
    case RP2040_SF_TABLE_FCMP_BASIC:
    case RP2040_SF_TABLE_FATAN2:
    case RP2040_SF_TABLE_INT642FLOAT:
    case RP2040_SF_TABLE_FIX642FLOAT:
    case RP2040_SF_TABLE_UINT642FLOAT:
    case RP2040_SF_TABLE_UFIX642FLOAT:
    case RP2040_SF_TABLE_FLOAT2INT64:
    case RP2040_SF_TABLE_FLOAT2FIX64:
    case RP2040_SF_TABLE_FLOAT2UINT64:
    case RP2040_SF_TABLE_FLOAT2UFIX64:
    case RP2040_SF_TABLE_FLOAT2DOUBLE:
        return true;
    default:
        return false;
    }
}

static void rp2040_install_bootrom_fp_stub(uint8_t *rom, uint32_t offset,
                                          uint32_t command)
{
    memcpy(rom + offset, rp2040_bootrom_fp_stub,
           sizeof(rp2040_bootrom_fp_stub));
    rp2040_store_word(rom, offset + RP2040_BOOTROM_FP_STUB_CODE_LITERAL_OFFSET,
                      command);
}

static void rp2040_install_synthetic_bootrom(void)
{
    g_autofree uint8_t *rom = g_malloc0(RP2040_ROM_SIZE);
    uint32_t func_base = RP2040_BOOTROM_STUBS_OFFSET;
    uint32_t func_table = RP2040_BOOTROM_FUNC_TABLE_OFFSET;
    uint32_t data_table = RP2040_BOOTROM_DATA_TABLE_OFFSET;
    uint32_t float_nyi = RP2040_BOOTROM_FLOAT_NYI_STUB_OFFSET | 1;
    uint32_t double_nyi = RP2040_BOOTROM_DOUBLE_NYI_STUB_OFFSET | 1;
    int i;

    memcpy(rom, rp2040_bootrom, sizeof(rp2040_bootrom));
    memcpy(rom + RP2040_BOOTROM_HARDFAULT_EXIT_OFFSET,
           rp2040_bootrom_hardfault_exit,
           sizeof(rp2040_bootrom_hardfault_exit));
    rp2040_store_word(rom, 0x0c, RP2040_BOOTROM_HARDFAULT_EXIT_OFFSET | 1);
    rom[RP2040_BOOTROM_ROM_VERSION_OFFSET] =
        RP2040_BOOTROM_SYNTHETIC_ROM_VERSION;
    memcpy(rom + RP2040_BOOTROM_LOOKUP_OFFSET, rp2040_bootrom_lookup,
           sizeof(rp2040_bootrom_lookup));

    rp2040_store_hword(rom, RP2040_BOOTROM_FUNC_TABLE_PTR_OFFSET,
                       RP2040_BOOTROM_FUNC_TABLE_OFFSET);
    rp2040_store_hword(rom, RP2040_BOOTROM_DATA_TABLE_PTR_OFFSET,
                       RP2040_BOOTROM_DATA_TABLE_OFFSET);
    rp2040_store_hword(rom, RP2040_BOOTROM_TABLE_LOOKUP_PTR_OFFSET,
                       RP2040_BOOTROM_LOOKUP_OFFSET | 1);

    for (i = 0; i < ARRAY_SIZE(rp2040_bootrom_functions); i++) {
        const RP2040BootromFunction *func = &rp2040_bootrom_functions[i];
        uint32_t entry = func_base | 1;

        if (func->impl) {
            memcpy(rom + func_base, func->impl, func->impl_size);
            if (func->code_literal_offset != UINT32_MAX) {
                rp2040_store_word(rom, func_base + func->code_literal_offset,
                                  func->code);
            }
            func_base += ROUND_UP(func->impl_size, 4);
        } else {
            memcpy(rom + func_base, rp2040_bootrom_nyi_stub,
                   sizeof(rp2040_bootrom_nyi_stub));
            rp2040_store_word(rom,
                              func_base +
                              RP2040_BOOTROM_NYI_CODE_LITERAL_OFFSET,
                              func->code);
            func_base += ROUND_UP(sizeof(rp2040_bootrom_nyi_stub), 4);
        }

        rp2040_store_hword(rom, func_table +
                           i * RP2040_BOOTROM_FUNC_TABLE_ENTRY_SIZE,
                           func->code);
        rp2040_store_hword(rom, func_table +
                           i * RP2040_BOOTROM_FUNC_TABLE_ENTRY_SIZE + 2,
                           entry);
    }

    memcpy(rom + RP2040_BOOTROM_FLOAT_NYI_STUB_OFFSET, rp2040_bootrom_nyi_stub,
           sizeof(rp2040_bootrom_nyi_stub));
    rp2040_store_word(rom, RP2040_BOOTROM_FLOAT_NYI_STUB_OFFSET +
                      RP2040_BOOTROM_NYI_CODE_LITERAL_OFFSET,
                      RP2040_ROM_TABLE_CODE('S', 'F'));
    memcpy(rom + RP2040_BOOTROM_DOUBLE_NYI_STUB_OFFSET, rp2040_bootrom_nyi_stub,
           sizeof(rp2040_bootrom_nyi_stub));
    rp2040_store_word(rom, RP2040_BOOTROM_DOUBLE_NYI_STUB_OFFSET +
                      RP2040_BOOTROM_NYI_CODE_LITERAL_OFFSET,
                      RP2040_ROM_TABLE_CODE('S', 'D'));

    rom[RP2040_BOOTROM_FLOAT_TABLE_OFFSET] = RP2040_BOOTROM_FLOAT_TABLE_WORDS;
    rom[RP2040_BOOTROM_DOUBLE_TABLE_OFFSET] = RP2040_BOOTROM_FLOAT_TABLE_WORDS;
    for (i = 0; i < RP2040_BOOTROM_FLOAT_TABLE_WORDS; i++) {
        uint32_t table_offset = i * sizeof(uint32_t);

        if (rp2040_bootrom_fp_supported(table_offset)) {
            uint32_t float_stub = RP2040_BOOTROM_FLOAT_STUBS_OFFSET +
                                  i * RP2040_BOOTROM_FP_STUB_SIZE;
            uint32_t double_stub = RP2040_BOOTROM_DOUBLE_STUBS_OFFSET +
                                   i * RP2040_BOOTROM_FP_STUB_SIZE;

            rp2040_install_bootrom_fp_stub(rom, float_stub,
                                           RP2040_SYNTHETIC_FP_CMD_FLOAT |
                                           table_offset);
            rp2040_install_bootrom_fp_stub(rom, double_stub,
                                           RP2040_SYNTHETIC_FP_CMD_DOUBLE |
                                           table_offset);
            rp2040_store_word(rom, RP2040_BOOTROM_FLOAT_TABLE_OFFSET + 2 +
                              table_offset, float_stub | 1);
            rp2040_store_word(rom, RP2040_BOOTROM_DOUBLE_TABLE_OFFSET + 2 +
                              table_offset, double_stub | 1);
        } else {
            rp2040_store_word(rom, RP2040_BOOTROM_FLOAT_TABLE_OFFSET + 2 +
                              table_offset, float_nyi);
            rp2040_store_word(rom, RP2040_BOOTROM_DOUBLE_TABLE_OFFSET + 2 +
                              table_offset, double_nyi);
        }
    }

    for (i = 0; i < ARRAY_SIZE(rp2040_bootrom_data); i++) {
        const RP2040BootromData *data = &rp2040_bootrom_data[i];

        rp2040_store_hword(rom, data_table +
                           i * RP2040_BOOTROM_DATA_TABLE_ENTRY_SIZE,
                           data->code);
        rp2040_store_hword(rom, data_table +
                           i * RP2040_BOOTROM_DATA_TABLE_ENTRY_SIZE + 2,
                           data->offset);
    }

    rom_add_blob_fixed("rp2040.bootrom", rom, RP2040_ROM_SIZE,
                       RP2040_ROM_BASE);
}

static uint32_t rp2040_apply_atomic_alias(uint32_t old, uint32_t value,
                                          hwaddr alias)
{
    switch (alias) {
    case ATOMIC_XOR:
        return old ^ value;
    case ATOMIC_SET:
        return old | value;
    case ATOMIC_CLR:
        return old & ~value;
    default:
        return value;
    }
}

static MemTxResult rp2040_powered_off_read(void *opaque, hwaddr addr,
                                           uint64_t *data, unsigned size,
                                           MemTxAttrs attrs)
{
    *data = 0;
    return MEMTX_ERROR;
}

static MemTxResult rp2040_powered_off_write(void *opaque, hwaddr addr,
                                            uint64_t data, unsigned size,
                                            MemTxAttrs attrs)
{
    return MEMTX_ERROR;
}

static const MemoryRegionOps rp2040_powered_off_ops = {
    .read_with_attrs = rp2040_powered_off_read,
    .write_with_attrs = rp2040_powered_off_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = {
        .min_access_size = 1,
        .max_access_size = 4,
    },
};

static void rp2040_update_mempowerdown(RP2040State *s)
{
    uint32_t mempowerdown;
    int i;

    if (!s->mempowerdown_ready) {
        return;
    }

    mempowerdown = rp2040_syscfg_get_mempowerdown(&s->syscfg);

    for (i = 0; i < ARRAY_SIZE(s->sram_poweroff); i++) {
        memory_region_set_enabled(&s->sram_poweroff[i],
                                  mempowerdown & BIT(i));
    }

    memory_region_set_enabled(&s->usbctrl_dpram_poweroff,
                              mempowerdown & BIT(6));
    memory_region_set_enabled(&s->rom_poweroff, mempowerdown & BIT(7));
}

static void rp2040_update_nmi(RP2040State *s)
{
    uint32_t nmi_mask = rp2040_syscfg_get_proc0_nmi_mask(&s->syscfg);
    bool nmi_level = false;
    int i;

    for (i = 0; i < RP2040_NUM_IRQS; i++) {
        bool irq_level = s->irq_level[0][i];
        bool route_to_nmi = nmi_mask & BIT(i);

        qemu_set_irq(s->cpu_irq[0][i], irq_level && !route_to_nmi);
        nmi_level |= irq_level && route_to_nmi;
    }

    qemu_set_irq(s->nmi_irq[0], nmi_level);
}

static void rp2040_syscfg_update(void *opaque)
{
    RP2040State *s = opaque;

    rp2040_update_mempowerdown(s);
    rp2040_update_nmi(s);
}

static void rp2040_start_core1_async_work(CPUState *cs, run_on_cpu_data data)
{
    ARMCPU *cpu = ARM_CPU(cs);
    CPUARMState *env = &cpu->env;

    cpu_reset(cs);
    cpu->power_state = PSCI_ON;
    env->halt_reason = NOT_HALTED;
    arm_rebuild_hflags(env);
    cs->halted = 0;
    cpu_resume(cs);
}

static bool rp2040_core1_powered_off(RP2040State *s)
{
    ARMCPU *cpu = s->armv7m[RP2040_PROC1].cpu;

    return !cpu || cpu->power_state == PSCI_OFF;
}

static void rp2040_start_core1(RP2040State *s)
{
    if (!s->armv7m[RP2040_PROC1].cpu ||
        !rp2040_core1_powered_off(s)) {
        return;
    }

    async_run_on_cpu(CPU(s->armv7m[RP2040_PROC1].cpu),
                     rp2040_start_core1_async_work,
                     RUN_ON_CPU_NULL);
}

static void rp2040_stop_core1_async_work(CPUState *cs, run_on_cpu_data data)
{
    ARMCPU *cpu = ARM_CPU(cs);

    cpu->power_state = PSCI_OFF;
    cpu->env.halt_reason = HALT_PSCI;
    cs->halted = 1;
    cs->exception_index = EXCP_HLT;
}

static void rp2040_stop_core1(RP2040State *s)
{
    if (!s->armv7m[RP2040_PROC1].cpu ||
        rp2040_core1_powered_off(s)) {
        return;
    }

    async_run_on_cpu(CPU(s->armv7m[RP2040_PROC1].cpu),
                     rp2040_stop_core1_async_work,
                     RUN_ON_CPU_NULL);
}

static void rp2040_psm_update(void *opaque)
{
    RP2040State *s = opaque;
    bool proc1_forced_off = rp2040_psm_get_frce_off(&s->psm) &
                            RP2040_PSM_PROC1;

    if (proc1_forced_off) {
        rp2040_stop_core1(s);
    } else {
        rp2040_start_core1(s);
    }
}

static const char *rp2040_bootrom_function_name(uint16_t code)
{
    int i;

    for (i = 0; i < ARRAY_SIZE(rp2040_bootrom_functions); i++) {
        if (rp2040_bootrom_functions[i].code == code) {
            return rp2040_bootrom_functions[i].name;
        }
    }

    return NULL;
}

static void rp2040_init_float_status(float_status *status)
{
    *status = (float_status) { 0 };
    set_float_rounding_mode(float_round_nearest_even, status);
}

static void rp2040_synthetic_fp_nyi(bool is_double, uint32_t offset)
{
    g_autofree char *detail = g_strdup_printf("%s table offset 0x%02" PRIx32,
                                              is_double ? "double" : "float",
                                              offset);

    rp2040_log_nyi("bootrom", "floating-point helper", detail);
}

static float rp2040_u32_to_host_float(uint32_t value)
{
    union {
        uint32_t u;
        float f;
    } v = { .u = value };

    return v.f;
}

static uint32_t rp2040_host_float_to_u32(float value)
{
    union {
        float f;
        uint32_t u;
    } v = { .f = value };

    return v.u;
}

static double rp2040_u64_to_host_double(uint64_t value)
{
    union {
        uint64_t u;
        double d;
    } v = { .u = value };

    return v.d;
}

static uint64_t rp2040_host_double_to_u64(double value)
{
    union {
        double d;
        uint64_t u;
    } v = { .d = value };

    return v.u;
}

static uint32_t rp2040_synthetic_fp_compare(double a, double b)
{
    if (isnan(a) || isnan(b)) {
        return 1;
    }
    if (a < b) {
        return -1;
    }
    if (a > b) {
        return 1;
    }
    return 0;
}

static int32_t rp2040_synthetic_double_to_i32(double value)
{
    if (isnan(value)) {
        return 0;
    }
    if (value >= INT32_MAX) {
        return INT32_MAX;
    }
    if (value <= INT32_MIN) {
        return INT32_MIN;
    }
    return value;
}

static uint32_t rp2040_synthetic_double_to_u32(double value)
{
    if (isnan(value) || value <= 0) {
        return 0;
    }
    if (value >= UINT32_MAX) {
        return UINT32_MAX;
    }
    return value;
}

static int64_t rp2040_synthetic_double_to_i64(double value)
{
    if (isnan(value)) {
        return 0;
    }
    if (value >= 0x1p63) {
        return INT64_MAX;
    }
    if (value <= -0x1p63) {
        return INT64_MIN;
    }
    return value;
}

static uint64_t rp2040_synthetic_double_to_u64(double value)
{
    if (isnan(value) || value <= 0) {
        return 0;
    }
    if (value >= 0x1p64) {
        return UINT64_MAX;
    }
    return value;
}

static void rp2040_synthetic_float_op(RP2040State *s, uint32_t offset)
{
    float_status status;
    float32 a = make_float32(s->synthetic_rom_dbg_arg[0]);
    float32 b = make_float32(s->synthetic_rom_dbg_arg[1]);
    float host_a = rp2040_u32_to_host_float(s->synthetic_rom_dbg_arg[0]);
    float host_b = rp2040_u32_to_host_float(s->synthetic_rom_dbg_arg[1]);
    float32 r32;
    float64 r64;
    uint64_t r;
    double scaled;

    rp2040_init_float_status(&status);
    memset(s->synthetic_rom_dbg_result, 0, sizeof(s->synthetic_rom_dbg_result));
    s->synthetic_rom_dbg_result[1] = 0;

    switch (offset) {
    case RP2040_SF_TABLE_FADD:
        s->synthetic_rom_dbg_result[0] =
            float32_val(float32_add(a, b, &status));
        break;
    case RP2040_SF_TABLE_FSUB:
        s->synthetic_rom_dbg_result[0] =
            float32_val(float32_sub(a, b, &status));
        break;
    case RP2040_SF_TABLE_FMUL:
        s->synthetic_rom_dbg_result[0] =
            float32_val(float32_mul(a, b, &status));
        break;
    case RP2040_SF_TABLE_FDIV:
        s->synthetic_rom_dbg_result[0] =
            float32_val(float32_div(a, b, &status));
        break;
    case RP2040_SF_TABLE_FCMP_FAST:
    case RP2040_SF_TABLE_FCMP_FAST_FLAGS:
    case RP2040_SF_TABLE_FCMP_BASIC:
        s->synthetic_rom_dbg_result[0] =
            rp2040_synthetic_fp_compare(host_a, host_b);
        break;
    case RP2040_SF_TABLE_FSQRT:
        s->synthetic_rom_dbg_result[0] =
            float32_val(float32_sqrt(a, &status));
        break;
    case RP2040_SF_TABLE_FLOAT2INT:
        s->synthetic_rom_dbg_result[0] =
            float32_to_int32_round_to_zero(a, &status);
        break;
    case RP2040_SF_TABLE_FLOAT2FIX:
        scaled = ldexp((double)host_a, s->synthetic_rom_dbg_arg[1] & 0xff);
        s->synthetic_rom_dbg_result[0] =
            rp2040_synthetic_double_to_i32(scaled);
        break;
    case RP2040_SF_TABLE_FLOAT2UINT:
        s->synthetic_rom_dbg_result[0] =
            float32_to_uint32_round_to_zero(a, &status);
        break;
    case RP2040_SF_TABLE_FLOAT2UFIX:
        scaled = ldexp((double)host_a, s->synthetic_rom_dbg_arg[1] & 0xff);
        s->synthetic_rom_dbg_result[0] =
            rp2040_synthetic_double_to_u32(scaled);
        break;
    case RP2040_SF_TABLE_INT2FLOAT:
        s->synthetic_rom_dbg_result[0] =
            float32_val(int32_to_float32(s->synthetic_rom_dbg_arg[0],
                                         &status));
        break;
    case RP2040_SF_TABLE_FIX2FLOAT:
        r32 = float64_to_float32(make_float64(rp2040_host_double_to_u64(
                                  ldexp((double)(int32_t)
                                        s->synthetic_rom_dbg_arg[0],
                                        -(int)(s->synthetic_rom_dbg_arg[1] &
                                               0xff)))),
                                 &status);
        s->synthetic_rom_dbg_result[0] = float32_val(r32);
        break;
    case RP2040_SF_TABLE_UINT2FLOAT:
        s->synthetic_rom_dbg_result[0] =
            float32_val(uint32_to_float32(s->synthetic_rom_dbg_arg[0],
                                          &status));
        break;
    case RP2040_SF_TABLE_UFIX2FLOAT:
        r32 = float64_to_float32(make_float64(rp2040_host_double_to_u64(
                                  ldexp((double)
                                        s->synthetic_rom_dbg_arg[0],
                                        -(int)(s->synthetic_rom_dbg_arg[1] &
                                               0xff)))),
                                 &status);
        s->synthetic_rom_dbg_result[0] = float32_val(r32);
        break;
    case RP2040_SF_TABLE_FCOS:
        s->synthetic_rom_dbg_result[0] =
            rp2040_host_float_to_u32(cosf(host_a));
        break;
    case RP2040_SF_TABLE_FSIN:
        s->synthetic_rom_dbg_result[0] =
            rp2040_host_float_to_u32(sinf(host_a));
        s->synthetic_rom_dbg_result[1] =
            rp2040_host_float_to_u32(cosf(host_a));
        break;
    case RP2040_SF_TABLE_FTAN:
        s->synthetic_rom_dbg_result[0] =
            rp2040_host_float_to_u32(tanf(host_a));
        break;
    case RP2040_SF_TABLE_V3_FSINCOS:
        s->synthetic_rom_dbg_result[0] =
            rp2040_host_float_to_u32(sinf(host_a));
        s->synthetic_rom_dbg_result[1] =
            rp2040_host_float_to_u32(cosf(host_a));
        break;
    case RP2040_SF_TABLE_FEXP:
        s->synthetic_rom_dbg_result[0] =
            rp2040_host_float_to_u32(expf(host_a));
        break;
    case RP2040_SF_TABLE_FLN:
        s->synthetic_rom_dbg_result[0] =
            rp2040_host_float_to_u32(logf(host_a));
        break;
    case RP2040_SF_TABLE_FATAN2:
        s->synthetic_rom_dbg_result[0] =
            rp2040_host_float_to_u32(atan2f(host_a, host_b));
        break;
    case RP2040_SF_TABLE_INT642FLOAT:
        r = deposit64(s->synthetic_rom_dbg_arg[0], 32, 32,
                      s->synthetic_rom_dbg_arg[1]);
        r32 = int64_to_float32((int64_t)r, &status);
        s->synthetic_rom_dbg_result[0] = float32_val(r32);
        break;
    case RP2040_SF_TABLE_FIX642FLOAT:
        r = deposit64(s->synthetic_rom_dbg_arg[0], 32, 32,
                      s->synthetic_rom_dbg_arg[1]);
        r32 = float64_to_float32(make_float64(rp2040_host_double_to_u64(
                                  ldexp((double)(int64_t)r,
                                        -(int)(s->synthetic_rom_dbg_arg[2] &
                                               0xff)))),
                                 &status);
        s->synthetic_rom_dbg_result[0] = float32_val(r32);
        break;
    case RP2040_SF_TABLE_UINT642FLOAT:
        r = deposit64(s->synthetic_rom_dbg_arg[0], 32, 32,
                      s->synthetic_rom_dbg_arg[1]);
        r32 = uint64_to_float32(r, &status);
        s->synthetic_rom_dbg_result[0] = float32_val(r32);
        break;
    case RP2040_SF_TABLE_UFIX642FLOAT:
        r = deposit64(s->synthetic_rom_dbg_arg[0], 32, 32,
                      s->synthetic_rom_dbg_arg[1]);
        r32 = float64_to_float32(make_float64(rp2040_host_double_to_u64(
                                  ldexp((double)r,
                                        -(int)(s->synthetic_rom_dbg_arg[2] &
                                               0xff)))),
                                 &status);
        s->synthetic_rom_dbg_result[0] = float32_val(r32);
        break;
    case RP2040_SF_TABLE_FLOAT2INT64:
        r = float32_to_int64_round_to_zero(a, &status);
        s->synthetic_rom_dbg_result[0] = r;
        s->synthetic_rom_dbg_result[1] = r >> 32;
        break;
    case RP2040_SF_TABLE_FLOAT2FIX64:
        scaled = ldexp((double)host_a, s->synthetic_rom_dbg_arg[1] & 0xff);
        r = rp2040_synthetic_double_to_i64(scaled);
        s->synthetic_rom_dbg_result[0] = r;
        s->synthetic_rom_dbg_result[1] = r >> 32;
        break;
    case RP2040_SF_TABLE_FLOAT2UINT64:
        r = float32_to_uint64_round_to_zero(a, &status);
        s->synthetic_rom_dbg_result[0] = r;
        s->synthetic_rom_dbg_result[1] = r >> 32;
        break;
    case RP2040_SF_TABLE_FLOAT2UFIX64:
        scaled = ldexp((double)host_a, s->synthetic_rom_dbg_arg[1] & 0xff);
        r = rp2040_synthetic_double_to_u64(scaled);
        s->synthetic_rom_dbg_result[0] = r;
        s->synthetic_rom_dbg_result[1] = r >> 32;
        break;
    case RP2040_SF_TABLE_FLOAT2DOUBLE:
        r64 = float32_to_float64(a, &status);
        r = float64_val(r64);
        s->synthetic_rom_dbg_result[0] = r;
        s->synthetic_rom_dbg_result[1] = r >> 32;
        break;
    default:
        s->synthetic_rom_dbg_result[0] = 0;
        rp2040_synthetic_fp_nyi(false, offset);
        break;
    }
}

static void rp2040_synthetic_double_op(RP2040State *s, uint32_t offset)
{
    float_status status;
    uint64_t av = deposit64(s->synthetic_rom_dbg_arg[0], 32, 32,
                            s->synthetic_rom_dbg_arg[1]);
    uint64_t bv = deposit64(s->synthetic_rom_dbg_arg[2], 32, 32,
                            s->synthetic_rom_dbg_arg[3]);
    float64 a = make_float64(av);
    float64 b = make_float64(bv);
    double host_a = rp2040_u64_to_host_double(av);
    double host_b = rp2040_u64_to_host_double(bv);
    float64 r64;
    float32 r32;
    uint64_t r;
    double scaled;

    rp2040_init_float_status(&status);
    memset(s->synthetic_rom_dbg_result, 0, sizeof(s->synthetic_rom_dbg_result));

    switch (offset) {
    case RP2040_SF_TABLE_FADD:
        r64 = float64_add(a, b, &status);
        goto return_double;
    case RP2040_SF_TABLE_FSUB:
        r64 = float64_sub(a, b, &status);
        goto return_double;
    case RP2040_SF_TABLE_FMUL:
        r64 = float64_mul(a, b, &status);
        goto return_double;
    case RP2040_SF_TABLE_FDIV:
        r64 = float64_div(a, b, &status);
        goto return_double;
    case RP2040_SF_TABLE_FCMP_FAST:
    case RP2040_SF_TABLE_FCMP_FAST_FLAGS:
    case RP2040_SF_TABLE_FCMP_BASIC:
        s->synthetic_rom_dbg_result[0] =
            rp2040_synthetic_fp_compare(host_a, host_b);
        s->synthetic_rom_dbg_result[1] = 0;
        break;
    case RP2040_SF_TABLE_FSQRT:
        r64 = float64_sqrt(a, &status);
        goto return_double;
    case RP2040_SF_TABLE_FLOAT2INT:
        s->synthetic_rom_dbg_result[0] =
            float64_to_int32_round_to_zero(a, &status);
        s->synthetic_rom_dbg_result[1] = 0;
        break;
    case RP2040_SF_TABLE_FLOAT2FIX:
        scaled = ldexp(host_a, s->synthetic_rom_dbg_arg[2] & 0xff);
        s->synthetic_rom_dbg_result[0] =
            rp2040_synthetic_double_to_i32(scaled);
        s->synthetic_rom_dbg_result[1] = 0;
        break;
    case RP2040_SF_TABLE_FLOAT2UINT:
        s->synthetic_rom_dbg_result[0] =
            float64_to_uint32_round_to_zero(a, &status);
        s->synthetic_rom_dbg_result[1] = 0;
        break;
    case RP2040_SF_TABLE_FLOAT2UFIX:
        scaled = ldexp(host_a, s->synthetic_rom_dbg_arg[2] & 0xff);
        s->synthetic_rom_dbg_result[0] =
            rp2040_synthetic_double_to_u32(scaled);
        s->synthetic_rom_dbg_result[1] = 0;
        break;
    case RP2040_SF_TABLE_INT2FLOAT:
        r64 = int32_to_float64(s->synthetic_rom_dbg_arg[0], &status);
        goto return_double;
    case RP2040_SF_TABLE_FIX2FLOAT:
        r64 = make_float64(rp2040_host_double_to_u64(
              ldexp((double)(int32_t)s->synthetic_rom_dbg_arg[0],
                    -(int)(s->synthetic_rom_dbg_arg[1] & 0xff))));
        goto return_double;
    case RP2040_SF_TABLE_UINT2FLOAT:
        r64 = uint32_to_float64(s->synthetic_rom_dbg_arg[0], &status);
        goto return_double;
    case RP2040_SF_TABLE_UFIX2FLOAT:
        r64 = make_float64(rp2040_host_double_to_u64(
              ldexp((double)s->synthetic_rom_dbg_arg[0],
                    -(int)(s->synthetic_rom_dbg_arg[1] & 0xff))));
        goto return_double;
    case RP2040_SF_TABLE_FCOS:
        r64 = make_float64(rp2040_host_double_to_u64(cos(host_a)));
        goto return_double;
    case RP2040_SF_TABLE_FSIN:
        r64 = make_float64(rp2040_host_double_to_u64(sin(host_a)));
        goto return_double;
    case RP2040_SF_TABLE_FTAN:
        r64 = make_float64(rp2040_host_double_to_u64(tan(host_a)));
        goto return_double;
    case RP2040_SF_TABLE_V3_FSINCOS:
        r = rp2040_host_double_to_u64(sin(host_a));
        s->synthetic_rom_dbg_result[0] = r;
        s->synthetic_rom_dbg_result[1] = r >> 32;
        r = rp2040_host_double_to_u64(cos(host_a));
        s->synthetic_rom_dbg_result[2] = r;
        s->synthetic_rom_dbg_result[3] = r >> 32;
        break;
    case RP2040_SF_TABLE_FEXP:
        r64 = make_float64(rp2040_host_double_to_u64(exp(host_a)));
        goto return_double;
    case RP2040_SF_TABLE_FLN:
        r64 = make_float64(rp2040_host_double_to_u64(log(host_a)));
        goto return_double;
    case RP2040_SF_TABLE_FATAN2:
        r64 = make_float64(rp2040_host_double_to_u64(atan2(host_a, host_b)));
        goto return_double;
    case RP2040_SF_TABLE_INT642FLOAT:
        r64 = int64_to_float64((int64_t)av, &status);
        goto return_double;
    case RP2040_SF_TABLE_FIX642FLOAT:
        r64 = make_float64(rp2040_host_double_to_u64(
              ldexp((double)(int64_t)av,
                    -(int)(s->synthetic_rom_dbg_arg[2] & 0xff))));
        goto return_double;
    case RP2040_SF_TABLE_UINT642FLOAT:
        r64 = uint64_to_float64(av, &status);
        goto return_double;
    case RP2040_SF_TABLE_UFIX642FLOAT:
        r64 = make_float64(rp2040_host_double_to_u64(
              ldexp((double)av,
                    -(int)(s->synthetic_rom_dbg_arg[2] & 0xff))));
        goto return_double;
    case RP2040_SF_TABLE_FLOAT2INT64:
        r = float64_to_int64_round_to_zero(a, &status);
        s->synthetic_rom_dbg_result[0] = r;
        s->synthetic_rom_dbg_result[1] = r >> 32;
        break;
    case RP2040_SF_TABLE_FLOAT2FIX64:
        scaled = ldexp(host_a, s->synthetic_rom_dbg_arg[2] & 0xff);
        r = rp2040_synthetic_double_to_i64(scaled);
        s->synthetic_rom_dbg_result[0] = r;
        s->synthetic_rom_dbg_result[1] = r >> 32;
        break;
    case RP2040_SF_TABLE_FLOAT2UINT64:
        r = float64_to_uint64_round_to_zero(a, &status);
        s->synthetic_rom_dbg_result[0] = r;
        s->synthetic_rom_dbg_result[1] = r >> 32;
        break;
    case RP2040_SF_TABLE_FLOAT2UFIX64:
        scaled = ldexp(host_a, s->synthetic_rom_dbg_arg[2] & 0xff);
        r = rp2040_synthetic_double_to_u64(scaled);
        s->synthetic_rom_dbg_result[0] = r;
        s->synthetic_rom_dbg_result[1] = r >> 32;
        break;
    case RP2040_SF_TABLE_FLOAT2DOUBLE:
        r32 = float64_to_float32(a, &status);
        s->synthetic_rom_dbg_result[0] = float32_val(r32);
        s->synthetic_rom_dbg_result[1] = 0;
        break;
    default:
        s->synthetic_rom_dbg_result[0] = 0;
        s->synthetic_rom_dbg_result[1] = 0;
        rp2040_synthetic_fp_nyi(true, offset);
        break;
    }
    return;

return_double:
    r = float64_val(r64);
    s->synthetic_rom_dbg_result[0] = r;
    s->synthetic_rom_dbg_result[1] = r >> 32;
}

static bool rp2040_synthetic_fp_op(RP2040State *s, uint32_t command)
{
    uint32_t offset = command & 0xff;

    switch (command & RP2040_SYNTHETIC_FP_CMD_MASK) {
    case RP2040_SYNTHETIC_FP_CMD_FLOAT:
        rp2040_synthetic_float_op(s, offset);
        return true;
    case RP2040_SYNTHETIC_FP_CMD_DOUBLE:
        rp2040_synthetic_double_op(s, offset);
        return true;
    default:
        return false;
    }
}

static void rp2040_synthetic_flash_helper_hit(RP2040State *s,
                                              unsigned int helper,
                                              const char *name)
{
    uint32_t count;

    g_assert(helper < RP2040_SYNTHETIC_ROM_FLASH_HELPER_COUNT);

    count = ++s->synthetic_rom_flash_helper_count[helper];
    trace_rp2040_synthetic_flash_helper(name, count);
}

static void rp2040_synthetic_rom_dbg_write(void *opaque, hwaddr addr,
                                           uint64_t value, unsigned size)
{
    RP2040State *s = opaque;
    hwaddr offset = addr & 0xfff;
    uint32_t command = value;
    uint16_t code = value;
    const char *name = rp2040_bootrom_function_name(code);
    char feature[64];
    char detail[64];
    Error *local_err = NULL;

    switch (offset) {
    case RP2040_SYNTHETIC_ROM_DBG_ARG0:
    case RP2040_SYNTHETIC_ROM_DBG_ARG1:
    case RP2040_SYNTHETIC_ROM_DBG_ARG2:
    case RP2040_SYNTHETIC_ROM_DBG_ARG3:
        s->synthetic_rom_dbg_arg[(offset - RP2040_SYNTHETIC_ROM_DBG_ARG0) /
                                 sizeof(uint32_t)] = value;
        return;
    case RP2040_SYNTHETIC_ROM_DBG_CMD:
        break;
    default:
        rp2040_log_nyi("bootrom", "synthetic diagnostic register write",
                       "unknown register");
        return;
    }

    if (rp2040_synthetic_fp_op(s, command)) {
        return;
    }

    if (command == RP2040_SYNTHETIC_ROM_DBG_CMD_EXIT) {
        qemu_system_shutdown_request_with_code(SHUTDOWN_CAUSE_GUEST_SHUTDOWN,
                                               s->synthetic_rom_dbg_arg[0]);
        return;
    }

    switch (code) {
    case RP2040_ROM_TABLE_CODE('C', 'X'):
        rp2040_synthetic_flash_helper_hit(
            s, RP2040_SYNTHETIC_FLASH_ENTER_CMD_XIP,
            "flash_enter_cmd_xip");
        rp2040_log_nyi("bootrom", "flash_enter_cmd_xip",
                       "synthetic helper does not reconfigure SSI hardware");
        return;
    case RP2040_ROM_TABLE_CODE('E', 'X'):
        rp2040_synthetic_flash_helper_hit(
            s, RP2040_SYNTHETIC_FLASH_EXIT_XIP, "flash_exit_xip");
        rp2040_log_nyi("bootrom", "flash_exit_xip",
                       "synthetic helper does not send serial flash commands");
        return;
    case RP2040_ROM_TABLE_CODE('F', 'C'):
        rp2040_synthetic_flash_helper_hit(
            s, RP2040_SYNTHETIC_FLASH_FLUSH_CACHE, "flash_flush_cache");
        rp2040_log_nyi("bootrom", "flash_flush_cache",
                       "XIP cache is not modeled");
        return;
    case RP2040_ROM_TABLE_CODE('I', 'F'):
        rp2040_synthetic_flash_helper_hit(
            s, RP2040_SYNTHETIC_FLASH_CONNECT_INTERNAL_FLASH,
            "connect_internal_flash");
        rp2040_log_nyi("bootrom", "connect_internal_flash",
                       "synthetic helper assumes the QSPI flash is connected");
        return;
    case RP2040_ROM_TABLE_CODE('R', 'E'):
        rp2040_synthetic_flash_helper_hit(
            s, RP2040_SYNTHETIC_FLASH_RANGE_ERASE, "flash_range_erase");
        if (!rp2040_xip_flash_range_erase(&s->xip,
                                          s->synthetic_rom_dbg_arg[0],
                                          s->synthetic_rom_dbg_arg[1],
                                          s->synthetic_rom_dbg_arg[2],
                                          s->synthetic_rom_dbg_arg[3],
                                          &local_err)) {
            warn_report_err(local_err);
        }
        return;
    case RP2040_ROM_TABLE_CODE('R', 'P'):
        rp2040_synthetic_flash_helper_hit(
            s, RP2040_SYNTHETIC_FLASH_RANGE_PROGRAM, "flash_range_program");
        if (!rp2040_xip_flash_range_program(&s->xip,
                                            s->synthetic_rom_dbg_arg[0],
                                            s->synthetic_rom_dbg_arg[1],
                                            s->synthetic_rom_dbg_arg[2],
                                            &local_err)) {
            warn_report_err(local_err);
        }
        return;
    }

    snprintf(feature, sizeof(feature), "boot ROM function '%c%c'",
             code & 0xff, (code >> 8) & 0xff);
    snprintf(detail, sizeof(detail), "%s%s%s",
             name ? "SDK name: " : "",
             name ? name : "unknown lookup code",
             name ? "" : "");
    rp2040_log_nyi("bootrom", feature, detail);
}

static uint64_t rp2040_synthetic_rom_dbg_read(void *opaque, hwaddr addr,
                                              unsigned size)
{
    RP2040State *s = opaque;
    hwaddr offset = addr & 0xfff;

    if (offset >= RP2040_SYNTHETIC_ROM_DBG_ARG0 &&
        offset <= RP2040_SYNTHETIC_ROM_DBG_ARG3 &&
        QEMU_IS_ALIGNED(offset, sizeof(uint32_t))) {
        return s->synthetic_rom_dbg_arg[(offset -
                                         RP2040_SYNTHETIC_ROM_DBG_ARG0) /
                                        sizeof(uint32_t)];
    }
    if (offset >= RP2040_SYNTHETIC_ROM_DBG_RESULT0 &&
        offset <= RP2040_SYNTHETIC_ROM_DBG_RESULT3 &&
        QEMU_IS_ALIGNED(offset, sizeof(uint32_t))) {
        return s->synthetic_rom_dbg_result[(offset -
                                            RP2040_SYNTHETIC_ROM_DBG_RESULT0) /
                                           sizeof(uint32_t)];
    }
    if (offset >= RP2040_SYNTHETIC_ROM_DBG_FLASH_COUNT0 &&
        offset < RP2040_SYNTHETIC_ROM_DBG_FLASH_COUNT0 +
                 sizeof(s->synthetic_rom_flash_helper_count) &&
        QEMU_IS_ALIGNED(offset, sizeof(uint32_t))) {
        return s->synthetic_rom_flash_helper_count[
            (offset - RP2040_SYNTHETIC_ROM_DBG_FLASH_COUNT0) /
            sizeof(uint32_t)];
    }

    rp2040_log_nyi("bootrom", "synthetic diagnostic register read",
                   "unsupported register");
    return 0;
}

static const MemoryRegionOps rp2040_synthetic_rom_dbg_ops = {
    .read = rp2040_synthetic_rom_dbg_read,
    .write = rp2040_synthetic_rom_dbg_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = {
        .min_access_size = 4,
        .max_access_size = 4,
    },
};

static void rp2040_set_irq(void *opaque, int irq, int level)
{
    RP2040State *s = opaque;

    assert(irq >= 0 && irq < RP2040_NUM_IRQS);
    s->irq_level[0][irq] = level;
    rp2040_update_nmi(s);
}

static void rp2040_update_uart_pins(RP2040State *s)
{
    pl011_set_tx_connected(&s->uart0,
                           !s->strict_uart_pins ||
                           s->uart0_tx_pin_enabled);
    pl011_set_rx_connected(&s->uart0,
                           !s->strict_uart_pins ||
                           s->uart0_rx_pin_enabled);
    pl011_set_tx_connected(&s->uart1,
                           !s->strict_uart_pins ||
                           s->uart1_tx_pin_enabled);
    pl011_set_rx_connected(&s->uart1,
                           !s->strict_uart_pins ||
                           s->uart1_rx_pin_enabled);
}

static void rp2040_set_uart_pin(void *opaque, int pin, int level)
{
    RP2040State *s = opaque;

    switch (pin) {
    case 0:
        s->uart0_tx_pin_enabled = level;
        break;
    case 1:
        s->uart0_rx_pin_enabled = level;
        break;
    case 2:
        s->uart1_tx_pin_enabled = level;
        break;
    case 3:
        s->uart1_rx_pin_enabled = level;
        break;
    default:
        g_assert_not_reached();
    }

    rp2040_update_uart_pins(s);
}

static uint64_t rp2040_usbctrl_regs_read(void *opaque, hwaddr addr,
                                         unsigned size)
{
    RP2040State *s = opaque;
    hwaddr offset = addr & 0xfff;
    uint64_t value;

    switch (offset) {
    case USBCTRL_SIE_STATUS:
        value = s->usbctrl_reg[offset / sizeof(uint32_t)] |
                USBCTRL_SIE_STATUS_VBUS_DETECTED;
        break;
    case USBCTRL_BUFF_CPU_HANDLE:
    case USBCTRL_EP_ABORT_DONE:
    case USBCTRL_INTR:
        value = s->usbctrl_reg[offset / sizeof(uint32_t)];
        break;
    case USBCTRL_INTS:
        value = (s->usbctrl_reg[USBCTRL_INTR / sizeof(uint32_t)] |
                 s->usbctrl_reg[USBCTRL_INTF / sizeof(uint32_t)]) &
                s->usbctrl_reg[USBCTRL_INTE / sizeof(uint32_t)];
        break;
    default:
        if (offset < sizeof(s->usbctrl_reg)) {
            value = s->usbctrl_reg[offset / sizeof(uint32_t)];
        } else {
            value = 0;
            rp2040_log_unimplemented_read("usbctrl_regs", size,
                                          RP2040_USBCTRL_REGS_BASE + addr,
                                          offset, value);
        }
        break;
    }

    return value;
}

static void rp2040_usbctrl_regs_write(void *opaque, hwaddr addr,
                                      uint64_t value64, unsigned size)
{
    RP2040State *s = opaque;
    hwaddr alias = addr & ATOMIC_ALIAS_MASK;
    hwaddr offset = addr & 0xfff;
    uint32_t value = value64;
    uint32_t old;

    if (offset < sizeof(s->usbctrl_reg)) {
        old = s->usbctrl_reg[offset / sizeof(uint32_t)];
        s->usbctrl_reg[offset / sizeof(uint32_t)] =
            rp2040_apply_atomic_alias(old, value, alias);
    } else {
        rp2040_log_unimplemented_write("usbctrl_regs", size,
                                       RP2040_USBCTRL_REGS_BASE + addr,
                                       offset, value64);
    }
}

static const MemoryRegionOps rp2040_usbctrl_regs_ops = {
    .read = rp2040_usbctrl_regs_read,
    .write = rp2040_usbctrl_regs_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = {
        .min_access_size = 4,
        .max_access_size = 4,
    },
};

static void rp2040_soc_init(Object *obj)
{
    RP2040State *s = RP2040(obj);
    int i;

    for (i = 0; i < RP2040_NUM_CORES; i++) {
        g_autofree char *name = g_strdup_printf("proc%d", i);

        object_initialize_child(obj, name, &s->armv7m[i], TYPE_ARMV7M);
        qdev_prop_set_string(DEVICE(&s->armv7m[i]), "cpu-type",
                             ARM_CPU_TYPE_NAME("cortex-m0"));
        qdev_prop_set_uint32(DEVICE(&s->armv7m[i]), "num-irq", 32);
    }
    object_initialize_child(obj, "uart0", &s->uart0, TYPE_PL011);
    object_property_add_alias(obj, "serial0", OBJECT(&s->uart0), "chardev");
    object_initialize_child(obj, "uart1", &s->uart1, TYPE_PL011);
    object_property_add_alias(obj, "serial1", OBJECT(&s->uart1), "chardev");

    object_initialize_child(obj, "xip", &s->xip, TYPE_RP2040_XIP);
    object_initialize_child(obj, "busctrl", &s->busctrl, TYPE_RP2040_BUSCTRL);
    object_initialize_child(obj, "clocks", &s->clocks, TYPE_RP2040_CLOCKS);
    object_initialize_child(obj, "dma", &s->dma, TYPE_RP2040_DMA);
    object_initialize_child(obj, "iobank0", &s->iobank0,
                            TYPE_RP2040_IOBANK0);
    object_initialize_child(obj, "ioqspi", &s->ioqspi, TYPE_RP2040_IOQSPI);
    object_initialize_child(obj, "pads-bank0", &s->pads_bank0,
                            TYPE_RP2040_PADS_BANK0);
    object_initialize_child(obj, "pads-qspi", &s->pads_qspi,
                            TYPE_RP2040_PADS_QSPI);
    object_initialize_child(obj, "pll-sys", &s->pll_sys, TYPE_RP2040_PLL);
    qdev_prop_set_string(DEVICE(&s->pll_sys), "trace-name",
                         "rp2040.pll_sys");
    qdev_prop_set_uint32(DEVICE(&s->pll_sys), "base", RP2040_PLL_SYS_BASE);
    qdev_prop_set_uint32(DEVICE(&s->pll_sys), "fallback-hz", 125000000);

    object_initialize_child(obj, "pll-usb", &s->pll_usb, TYPE_RP2040_PLL);
    qdev_prop_set_string(DEVICE(&s->pll_usb), "trace-name",
                         "rp2040.pll_usb");
    qdev_prop_set_uint32(DEVICE(&s->pll_usb), "base", RP2040_PLL_USB_BASE);
    qdev_prop_set_uint32(DEVICE(&s->pll_usb), "fallback-hz", 48000000);

    object_initialize_child(obj, "psm", &s->psm, TYPE_RP2040_PSM);
    object_initialize_child(obj, "resets", &s->resets, TYPE_RP2040_RESETS);
    object_initialize_child(obj, "rosc", &s->rosc, TYPE_RP2040_ROSC);
    object_initialize_child(obj, "sio", &s->sio, TYPE_RP2040_SIO);
    object_initialize_child(obj, "syscfg", &s->syscfg, TYPE_RP2040_SYSCFG);
    object_initialize_child(obj, "sysinfo", &s->sysinfo, TYPE_RP2040_SYSINFO);
    object_initialize_child(obj, "tbman", &s->tbman, TYPE_RP2040_TBMAN);
    object_initialize_child(obj, "timer", &s->timer, TYPE_RP2040_TIMER);
    object_initialize_child(obj, "vreg", &s->vreg, TYPE_RP2040_VREG);
    object_initialize_child(obj, "watchdog", &s->watchdog,
                            TYPE_RP2040_WATCHDOG);
    object_initialize_child(obj, "xosc", &s->xosc, TYPE_RP2040_XOSC);

    s->irq = qemu_allocate_irqs(rp2040_set_irq, s, RP2040_NUM_IRQS);
    s->sysclk = clock_new(obj, "sysclk");
}

static void rp2040_soc_realize(DeviceState *dev, Error **errp)
{
    RP2040State *s = RP2040(dev);
    Error *err = NULL;
    g_autofree char *filename = NULL;
    ssize_t image_size;
    int i;

    if (!s->board_memory) {
        error_setg(errp, "memory property was not set");
        return;
    }
    qdev_prop_set_bit(DEVICE(&s->armv7m[RP2040_PROC1]), "start-powered-off",
                      s->bootrom_file != NULL);

    if (!memory_region_init_rom(&s->rom, OBJECT(dev), "rp2040.rom",
                                RP2040_ROM_SIZE, errp)) {
        return;
    }
    memory_region_add_subregion(s->board_memory, RP2040_ROM_BASE, &s->rom);
    memory_region_init_io(&s->rom_poweroff, OBJECT(dev),
                          &rp2040_powered_off_ops, s,
                          "rp2040.rom.poweroff", RP2040_ROM_SIZE);
    memory_region_add_subregion_overlap(s->board_memory, RP2040_ROM_BASE,
                                        &s->rom_poweroff, 1);
    memory_region_set_enabled(&s->rom_poweroff, false);
    memory_region_init_io(&s->synthetic_rom_dbg, OBJECT(dev),
                          &rp2040_synthetic_rom_dbg_ops, s,
                          "rp2040.synthetic-rom-dbg",
                          RP2040_SYNTHETIC_ROM_DBG_SIZE);
    memory_region_add_subregion(s->board_memory,
                                RP2040_SYNTHETIC_ROM_DBG_BASE,
                                &s->synthetic_rom_dbg);

    if (s->bootrom_file) {
        filename = qemu_find_file(QEMU_FILE_TYPE_BIOS, s->bootrom_file);
        if (!filename) {
            error_setg(errp, "could not find RP2040 boot ROM image '%s'",
                       s->bootrom_file);
            return;
        }

        image_size = load_image_targphys(filename, RP2040_ROM_BASE,
                                         RP2040_ROM_SIZE, errp);
        if (image_size < 0) {
            return;
        }
    } else {
        rp2040_install_synthetic_bootrom();
    }

    if (!sysbus_realize(SYS_BUS_DEVICE(&s->xip), errp)) {
        return;
    }
    if (!s->bootrom_file) {
        rp2040_xip_set_synthetic_hardfault_vector(
            &s->xip, RP2040_BOOTROM_HARDFAULT_EXIT_OFFSET | 1);
    }
    sysbus_mmio_map(SYS_BUS_DEVICE(&s->xip), 0, RP2040_XIP_BASE);
    sysbus_mmio_map(SYS_BUS_DEVICE(&s->xip), 1, RP2040_XIP_CTRL_BASE);
    sysbus_mmio_map(SYS_BUS_DEVICE(&s->xip), 2, RP2040_XIP_SSI_BASE);
    sysbus_mmio_map(SYS_BUS_DEVICE(&s->xip), 3, RP2040_XIP_AUX_BASE);
    memory_region_add_subregion(get_system_memory(), RP2040_XIP_NOALLOC_BASE,
                                &s->xip.xip_noalloc);
    memory_region_add_subregion(get_system_memory(), RP2040_XIP_NOCACHE_BASE,
                                &s->xip.xip_nocache);
    memory_region_add_subregion(get_system_memory(),
                                RP2040_XIP_NOCACHE_NOALLOC_BASE,
                                &s->xip.xip_nocache_noalloc);

    if (!sysbus_realize(SYS_BUS_DEVICE(&s->busctrl), errp)) {
        return;
    }
    sysbus_mmio_map(SYS_BUS_DEVICE(&s->busctrl), 0, RP2040_BUSCTRL_BASE);

    object_property_set_link(OBJECT(&s->dma), "memory",
                             OBJECT(s->board_memory), &err);
    if (err != NULL) {
        error_propagate(errp, err);
        return;
    }
    if (!sysbus_realize(SYS_BUS_DEVICE(&s->dma), errp)) {
        return;
    }
    sysbus_mmio_map(SYS_BUS_DEVICE(&s->dma), 0, RP2040_DMA_BASE);
    sysbus_connect_irq(SYS_BUS_DEVICE(&s->dma), 0, s->irq[RP2040_DMA_IRQ_0]);
    sysbus_connect_irq(SYS_BUS_DEVICE(&s->dma), 1, s->irq[RP2040_DMA_IRQ_1]);
    qdev_connect_gpio_out_named(DEVICE(&s->xip), "dreq-rx", 0,
                                qdev_get_gpio_in_named(DEVICE(&s->dma),
                                                       "dreq",
                                                       RP2040_DREQ_XIP_SSIRX));
    qdev_connect_gpio_out_named(DEVICE(&s->xip), "dreq-stream", 0,
                                qdev_get_gpio_in_named(DEVICE(&s->dma),
                                                       "dreq",
                                                       RP2040_DREQ_XIP_STREAM));

    if (!sysbus_realize(SYS_BUS_DEVICE(&s->iobank0), errp)) {
        return;
    }
    sysbus_mmio_map(SYS_BUS_DEVICE(&s->iobank0), 0, RP2040_IOBANK0_BASE);
    sysbus_connect_irq(SYS_BUS_DEVICE(&s->iobank0), 0,
                       s->irq[RP2040_IO_IRQ_BANK0]);
    qdev_connect_gpio_out_named(DEVICE(&s->iobank0), "uart0-pin", 0,
                                qemu_allocate_irq(rp2040_set_uart_pin, s, 0));
    qdev_connect_gpio_out_named(DEVICE(&s->iobank0), "uart0-pin", 1,
                                qemu_allocate_irq(rp2040_set_uart_pin, s, 1));
    qdev_connect_gpio_out_named(DEVICE(&s->iobank0), "uart1-pin", 0,
                                qemu_allocate_irq(rp2040_set_uart_pin, s, 2));
    qdev_connect_gpio_out_named(DEVICE(&s->iobank0), "uart1-pin", 1,
                                qemu_allocate_irq(rp2040_set_uart_pin, s, 3));

    object_property_set_link(OBJECT(&s->ioqspi), "xip", OBJECT(&s->xip),
                             &error_abort);
    if (!sysbus_realize(SYS_BUS_DEVICE(&s->ioqspi), errp)) {
        return;
    }
    sysbus_mmio_map(SYS_BUS_DEVICE(&s->ioqspi), 0, RP2040_IOQSPI_BASE);

    if (!sysbus_realize(SYS_BUS_DEVICE(&s->pads_bank0), errp)) {
        return;
    }
    sysbus_mmio_map(SYS_BUS_DEVICE(&s->pads_bank0), 0,
                    RP2040_PADS_BANK0_BASE);

    if (!sysbus_realize(SYS_BUS_DEVICE(&s->pads_qspi), errp)) {
        return;
    }
    sysbus_mmio_map(SYS_BUS_DEVICE(&s->pads_qspi), 0,
                    RP2040_PADS_QSPI_BASE);

    if (!sysbus_realize(SYS_BUS_DEVICE(&s->pll_sys), errp)) {
        return;
    }
    sysbus_mmio_map(SYS_BUS_DEVICE(&s->pll_sys), 0, RP2040_PLL_SYS_BASE);

    if (!sysbus_realize(SYS_BUS_DEVICE(&s->pll_usb), errp)) {
        return;
    }
    sysbus_mmio_map(SYS_BUS_DEVICE(&s->pll_usb), 0, RP2040_PLL_USB_BASE);

    qdev_connect_clock_in(DEVICE(&s->clocks), "pll-sys",
                          qdev_get_clock_out(DEVICE(&s->pll_sys), "clk"));
    qdev_connect_clock_in(DEVICE(&s->clocks), "pll-usb",
                          qdev_get_clock_out(DEVICE(&s->pll_usb), "clk"));
    if (!sysbus_realize(SYS_BUS_DEVICE(&s->clocks), errp)) {
        return;
    }
    sysbus_mmio_map(SYS_BUS_DEVICE(&s->clocks), 0, RP2040_CLOCKS_BASE);
    clock_set_source(s->sysclk, qdev_get_clock_out(DEVICE(&s->clocks),
                                                   "clk-sys"));

    if (!sysbus_realize(SYS_BUS_DEVICE(&s->psm), errp)) {
        return;
    }
    sysbus_mmio_map(SYS_BUS_DEVICE(&s->psm), 0, RP2040_PSM_BASE);
    rp2040_psm_set_update_callback(&s->psm, rp2040_psm_update, s);

    if (!sysbus_realize(SYS_BUS_DEVICE(&s->resets), errp)) {
        return;
    }
    sysbus_mmio_map(SYS_BUS_DEVICE(&s->resets), 0, RP2040_RESETS_BASE);

    if (!sysbus_realize(SYS_BUS_DEVICE(&s->rosc), errp)) {
        return;
    }
    sysbus_mmio_map(SYS_BUS_DEVICE(&s->rosc), 0, RP2040_ROSC_BASE);

    if (!sysbus_realize(SYS_BUS_DEVICE(&s->sio), errp)) {
        return;
    }
    sysbus_mmio_map(SYS_BUS_DEVICE(&s->sio), 0, RP2040_SIO_BASE);
    sysbus_connect_irq(SYS_BUS_DEVICE(&s->sio), 0,
                       s->irq[RP2040_SIO_IRQ_PROC0]);

    if (!sysbus_realize(SYS_BUS_DEVICE(&s->syscfg), errp)) {
        return;
    }
    sysbus_mmio_map(SYS_BUS_DEVICE(&s->syscfg), 0, RP2040_SYSCFG_BASE);
    rp2040_syscfg_set_update_callback(&s->syscfg, rp2040_syscfg_update, s);

    if (!sysbus_realize(SYS_BUS_DEVICE(&s->sysinfo), errp)) {
        return;
    }
    sysbus_mmio_map(SYS_BUS_DEVICE(&s->sysinfo), 0, RP2040_SYSINFO_BASE);

    if (!sysbus_realize(SYS_BUS_DEVICE(&s->tbman), errp)) {
        return;
    }
    sysbus_mmio_map(SYS_BUS_DEVICE(&s->tbman), 0, RP2040_TBMAN_BASE);

    if (!sysbus_realize(SYS_BUS_DEVICE(&s->timer), errp)) {
        return;
    }
    sysbus_mmio_map(SYS_BUS_DEVICE(&s->timer), 0, RP2040_TIMER_BASE);
    for (i = 0; i < 4; i++) {
        sysbus_connect_irq(SYS_BUS_DEVICE(&s->timer), i, s->irq[i]);
    }

    if (!sysbus_realize(SYS_BUS_DEVICE(&s->vreg), errp)) {
        return;
    }
    sysbus_mmio_map(SYS_BUS_DEVICE(&s->vreg), 0, RP2040_VREG_BASE);

    qdev_connect_clock_in(DEVICE(&s->watchdog), "clk-ref",
                          qdev_get_clock_out(DEVICE(&s->clocks), "clk-ref"));
    if (!sysbus_realize(SYS_BUS_DEVICE(&s->watchdog), errp)) {
        return;
    }
    sysbus_mmio_map(SYS_BUS_DEVICE(&s->watchdog), 0, RP2040_WATCHDOG_BASE);

    if (!sysbus_realize(SYS_BUS_DEVICE(&s->xosc), errp)) {
        return;
    }
    sysbus_mmio_map(SYS_BUS_DEVICE(&s->xosc), 0, RP2040_XOSC_BASE);

    for (i = 0; i < 4; i++) {
        g_autofree char *name = g_strdup_printf("rp2040.sram%d", i);
        g_autofree char *poweroff_name =
            g_strdup_printf("rp2040.sram%d.poweroff", i);

        if (!memory_region_init_ram(&s->sram[i], OBJECT(dev), name,
                                    RP2040_SRAM_BANK_SIZE, errp)) {
            return;
        }
        memory_region_add_subregion(s->board_memory,
                                    RP2040_SRAM_BASE +
                                    i * RP2040_SRAM_BANK_SIZE,
                                    &s->sram[i]);
        memory_region_init_io(&s->sram_poweroff[i], OBJECT(dev),
                              &rp2040_powered_off_ops, s, poweroff_name,
                              RP2040_SRAM_BANK_SIZE);
        memory_region_add_subregion_overlap(s->board_memory,
                                            RP2040_SRAM_BASE +
                                            i * RP2040_SRAM_BANK_SIZE,
                                            &s->sram_poweroff[i], 1);
        memory_region_set_enabled(&s->sram_poweroff[i], false);
    }

    if (!memory_region_init_ram(&s->sram[4], OBJECT(dev), "rp2040.sram4",
                                RP2040_SRAM_HI_SIZE, errp)) {
        return;
    }
    memory_region_add_subregion(s->board_memory, RP2040_SRAM4_BASE,
                                &s->sram[4]);
    memory_region_init_io(&s->sram_poweroff[4], OBJECT(dev),
                          &rp2040_powered_off_ops, s,
                          "rp2040.sram4.poweroff", RP2040_SRAM_HI_SIZE);
    memory_region_add_subregion_overlap(s->board_memory, RP2040_SRAM4_BASE,
                                        &s->sram_poweroff[4], 1);
    memory_region_set_enabled(&s->sram_poweroff[4], false);

    if (!memory_region_init_ram(&s->sram[5], OBJECT(dev), "rp2040.sram5",
                                RP2040_SRAM_HI_SIZE, errp)) {
        return;
    }
    memory_region_add_subregion(s->board_memory, RP2040_SRAM5_BASE,
                                &s->sram[5]);
    memory_region_init_io(&s->sram_poweroff[5], OBJECT(dev),
                          &rp2040_powered_off_ops, s,
                          "rp2040.sram5.poweroff", RP2040_SRAM_HI_SIZE);
    memory_region_add_subregion_overlap(s->board_memory, RP2040_SRAM5_BASE,
                                        &s->sram_poweroff[5], 1);
    memory_region_set_enabled(&s->sram_poweroff[5], false);

    if (!memory_region_init_ram(&s->usbctrl_dpram, OBJECT(dev),
                                "rp2040.usbctrl_dpram",
                                RP2040_USBCTRL_DPRAM_SIZE, errp)) {
        return;
    }
    memory_region_add_subregion(s->board_memory, RP2040_USBCTRL_DPRAM_BASE,
                                &s->usbctrl_dpram);
    memory_region_init_io(&s->usbctrl_dpram_poweroff, OBJECT(dev),
                          &rp2040_powered_off_ops, s,
                          "rp2040.usbctrl_dpram.poweroff",
                          RP2040_USBCTRL_DPRAM_SIZE);
    memory_region_add_subregion_overlap(s->board_memory,
                                        RP2040_USBCTRL_DPRAM_BASE,
                                        &s->usbctrl_dpram_poweroff, 1);
    memory_region_set_enabled(&s->usbctrl_dpram_poweroff, false);

    s->mempowerdown_ready = true;
    rp2040_update_mempowerdown(s);

    memory_region_init_io(&s->usbctrl_regs, OBJECT(dev),
                          &rp2040_usbctrl_regs_ops, s,
                          "rp2040.usbctrl_regs",
                          RP2040_USBCTRL_REGS_SIZE);
    memory_region_add_subregion(s->board_memory, RP2040_USBCTRL_REGS_BASE,
                                &s->usbctrl_regs);

    for (i = 0; i < ARRAY_SIZE(rp2040_unimplemented); i++) {
        create_unimplemented_device(rp2040_unimplemented[i].name,
                                    rp2040_unimplemented[i].base,
                                    rp2040_unimplemented[i].size);
    }

    for (i = 0; i < RP2040_NUM_CORES; i++) {
        int irq;

        qdev_connect_clock_in(DEVICE(&s->armv7m[i]), "cpuclk", s->sysclk);
        g_autofree char *name = g_strdup_printf("rp2040.proc%d-memory", i);

        memory_region_init_alias(&s->cpu_memory[i], OBJECT(dev), name,
                                 s->board_memory, 0,
                                 memory_region_size(s->board_memory));
        object_property_set_link(OBJECT(&s->armv7m[i]), "memory",
                                 OBJECT(&s->cpu_memory[i]), &err);
        if (err != NULL) {
            error_propagate(errp, err);
            return;
        }

        if (!sysbus_realize(SYS_BUS_DEVICE(&s->armv7m[i]), errp)) {
            return;
        }
        for (irq = 0; irq < RP2040_NUM_IRQS; irq++) {
            s->cpu_irq[i][irq] = qdev_get_gpio_in(DEVICE(&s->armv7m[i]),
                                                  irq);
        }
        s->nmi_irq[i] = qdev_get_gpio_in_named(DEVICE(&s->armv7m[i]),
                                               "NMI", 0);
    }
    rp2040_update_nmi(s);
    sysbus_connect_irq(SYS_BUS_DEVICE(&s->iobank0), 1,
                       s->cpu_irq[RP2040_PROC1][RP2040_IO_IRQ_BANK0]);
    sysbus_connect_irq(SYS_BUS_DEVICE(&s->sio), 1,
                       s->cpu_irq[RP2040_PROC1][RP2040_SIO_IRQ_PROC1]);

    qdev_connect_clock_in(DEVICE(&s->uart0), "clk",
                          qdev_get_clock_out(DEVICE(&s->clocks), "clk-peri"));
    if (!sysbus_realize(SYS_BUS_DEVICE(&s->uart0), errp)) {
        return;
    }
    sysbus_mmio_map(SYS_BUS_DEVICE(&s->uart0), 0, RP2040_UART0_BASE);
    sysbus_mmio_map(SYS_BUS_DEVICE(&s->uart0), 1, RP2040_UART0_BASE + 0x1000);
    sysbus_connect_irq(SYS_BUS_DEVICE(&s->uart0), 0, s->irq[RP2040_UART0_IRQ]);
    qdev_connect_gpio_out_named(DEVICE(&s->uart0), "dreq-tx", 0,
                                qdev_get_gpio_in_named(DEVICE(&s->dma),
                                                       "dreq",
                                                       RP2040_DREQ_UART0_TX));
    qdev_connect_gpio_out_named(DEVICE(&s->uart0), "dreq-rx", 0,
                                qdev_get_gpio_in_named(DEVICE(&s->dma),
                                                       "dreq",
                                                       RP2040_DREQ_UART0_RX));

    qdev_connect_clock_in(DEVICE(&s->uart1), "clk",
                          qdev_get_clock_out(DEVICE(&s->clocks), "clk-peri"));
    if (!sysbus_realize(SYS_BUS_DEVICE(&s->uart1), errp)) {
        return;
    }
    sysbus_mmio_map(SYS_BUS_DEVICE(&s->uart1), 0, RP2040_UART1_BASE);
    sysbus_mmio_map(SYS_BUS_DEVICE(&s->uart1), 1, RP2040_UART1_BASE + 0x1000);
    sysbus_connect_irq(SYS_BUS_DEVICE(&s->uart1), 0, s->irq[RP2040_UART1_IRQ]);
    qdev_connect_gpio_out_named(DEVICE(&s->uart1), "dreq-tx", 0,
                                qdev_get_gpio_in_named(DEVICE(&s->dma),
                                                       "dreq",
                                                       RP2040_DREQ_UART1_TX));
    qdev_connect_gpio_out_named(DEVICE(&s->uart1), "dreq-rx", 0,
                                qdev_get_gpio_in_named(DEVICE(&s->dma),
                                                       "dreq",
                                                       RP2040_DREQ_UART1_RX));
    rp2040_update_uart_pins(s);
}

static const Property rp2040_soc_properties[] = {
    DEFINE_PROP_LINK("memory", RP2040State, board_memory, TYPE_MEMORY_REGION,
                     MemoryRegion *),
    DEFINE_PROP_STRING("bootrom-file", RP2040State, bootrom_file),
    DEFINE_PROP_BOOL("strict-uart-pins", RP2040State, strict_uart_pins, true),
};

static void rp2040_soc_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->realize = rp2040_soc_realize;
    device_class_set_props(dc, rp2040_soc_properties);
}

static const TypeInfo rp2040_soc_info = {
    .name          = TYPE_RP2040,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(RP2040State),
    .instance_init = rp2040_soc_init,
    .class_init    = rp2040_soc_class_init,
};

static void rp2040_soc_types(void)
{
    type_register_static(&rp2040_soc_info);
}
type_init(rp2040_soc_types)
