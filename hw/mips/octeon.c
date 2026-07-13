/*
 * QEMU Cavium Octeon board model.
 *
 * Copyright (c) 2026 Kirill A. Korinsky
 *
 * This work is based on Amir Mehmood's original Octeon host.patch.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#include "qemu/osdep.h"
#include "qemu/units.h"
#include "qemu/datadir.h"
#include "qemu/bswap.h"
#include "qemu/host-utils.h"
#include "qemu/timer.h"
#include "qapi/error.h"
#include "hw/char/serial-mm.h"
#include "hw/core/clock.h"
#include "hw/core/irq.h"
#include "hw/core/boards.h"
#include "hw/core/qdev-clock.h"
#include "hw/core/qdev-properties.h"
#include "hw/core/qdev-properties-system.h"
#include "hw/core/sysbus.h"
#include "hw/block/flash.h"
#include "hw/mips/mips.h"
#include "hw/pci/pci.h"
#include "hw/pci/pci_host.h"
#include "hw/usb/hcd-dwc3.h"
#include "system/blockdev.h"
#include "system/cpus.h"
#include "system/memory.h"
#include "system/reset.h"
#include "system/runstate.h"
#include "system/system.h"
#include "exec/cpu-interrupt.h"
#include "qemu/guest-random.h"
#include "qemu/error-report.h"
#include "qemu/atomic.h"
#include "target/mips/cpu.h"

#define TYPE_OCTEON_MACHINE MACHINE_TYPE_NAME("octeon3")
OBJECT_DECLARE_SIMPLE_TYPE(OcteonMachineState, OCTEON_MACHINE)

#define TYPE_OCTEON_PCI_HOST "octeon-pci-host"
OBJECT_DECLARE_SIMPLE_TYPE(OcteonPCIHostState, OCTEON_PCI_HOST)

#define OCTEON_CSR_32BIT_SIZE       4
#define OCTEON_CSR_64BIT_SIZE       8
#define OCTEON_CSR_HI32_OFFSET      0
#define OCTEON_CSR_LO32_OFFSET      4

#define OCTEON_MAX_CPUS             16
/*
 * Clock defaults follow a Ubiquiti E1000 EBB7304 U-Boot banner.
 * DDR follows the upstream EBB7304 U-Boot board default.
 */
#define OCTEON_DEFAULT_CPU_HZ       1800000000
#define OCTEON_DEFAULT_REF_HZ       50000000
#define OCTEON_DEFAULT_IO_HZ        1000000000
#define OCTEON_DEFAULT_DDR_HZ       800000000

/*
 * The physical layout follows U-Boot's generic Octeon III
 * OCTEON_MAX_PHY_MEM_SIZE value. The synthesized DDR4 SPD below accepts
 * power-of-two RAM sizes from 256 MiB through 32 GiB. Keep 1 GiB as the
 * tested default.
 */
#define OCTEON_DEFAULT_RAM_SIZE     (1 * GiB)
#define OCTEON_MAX_PHY_MEM_SIZE     (512 * GiB)

/*
 * The EBB7304 FDT exposes DRAM below and above the
 * 0x10000000..0x1fffffff boot-bus/EJTAG hole.
 */
#define OCTEON_DR0_BASE             0x000000000ULL
#define OCTEON_DR0_SIZE             (256 * MiB)
#define OCTEON_DR1_BASE             0x020000000ULL
#define OCTEON_DR1_SIZE             (OCTEON_MAX_PHY_MEM_SIZE - OCTEON_DR0_SIZE)

#define OCTEON_CIU_BASE             0x1070000000000ULL
#define OCTEON_CIU_LEGACY_PAGE_SIZE (4 * KiB)
#define OCTEON_CIU_SIZE             (1 * MiB + OCTEON_CIU_LEGACY_PAGE_SIZE)
#define OCTEON_CIU_FUSE             0x728
#define OCTEON_CIU_SUM_IPI          0x0000000100000000ULL
#define OCTEON_CIU_SUM_UART         0x0000000800000000ULL
#define OCTEON_CIU_SUM_PCI          0x0000001000000000ULL
#define OCTEON_CIU_ENABLE0          0x88006f1800000000ULL
#define OCTEON_CIU_IPI_SUM_BASE     0x008
#define OCTEON_CIU_IPI_SUM_STRIDE   0x10
#define OCTEON_CIU_IPI_EN_BASE      0x210
#define OCTEON_CIU_IPI_EN_STRIDE    0x20
#define OCTEON_CIU_MBOX_SET_BASE    0x600
#define OCTEON_CIU_MBOX_CLR_BASE    0x680
#define OCTEON_CIU_MBOX_SET_STRIDE  8
#define OCTEON_CIU_GPIO_RX_DAT      0x880
#define OCTEON_CIU_GPIO_TX_SET      0x888
#define OCTEON_CIU_GPIO_TX_CLR      0x890
#define OCTEON_CIU_GPIO_BIT_CFG_STRIDE OCTEON_CSR_64BIT_SIZE
#define OCTEON_CIU_GPIO_BIT_CFGX(x) \
    (0x900 + (x) * OCTEON_CIU_GPIO_BIT_CFG_STRIDE)
#define OCTEON_CIU_GPIO_COUNT       32
#define OCTEON_CIU_GPIO_INPUTS      ((1ULL << OCTEON_CIU_GPIO_COUNT) - 1)
#define OCTEON_CIU_GPIO_BIT_CFG_TX_OE (1ULL << 0)

#define OCTEON_CIU3_BASE            0x1010000000000ULL
#define OCTEON_CIU3_SIZE            0xb0000000ULL
#define OCTEON_CIU3_PP_RST          0x100
#define OCTEON_CIU3_PP_RST_PENDING  0x110
#define OCTEON_CIU3_NMI             0x160
#define OCTEON_CIU3_FUSE            0x1a0
#define OCTEON_CIU3_IDT_CTL(idt)    ((idt) * 8 + 0x110000U)
#define OCTEON_CIU3_IDT_PP(idt)     ((idt) * 32 + 0x120000U)
#define OCTEON_CIU3_IDT_IO(idt)     ((idt) * 8 + 0x130000U)
#define OCTEON_CIU3_DEST_PP_INT(cpu) ((cpu) * 8 + 0x200000U)
#define OCTEON_CIU3_DEST_PP_INT_INTSN_SHIFT 32
#define OCTEON_CIU3_DEST_PP_INT_INTR 0x0000000000000001ULL
#define OCTEON_CIU3_IDT_CTL_IP_NUM  0x7
#define OCTEON_CIU3_CP0_IRQ_BASE    2
#define OCTEON_CIU3_CP0_IRQ_COUNT   4
#define OCTEON_CIU3_ISC_CTL_BASE    0x80000000U
#define OCTEON_CIU3_ISC_W1C_BASE    0x90000000U
#define OCTEON_CIU3_ISC_W1S_BASE    0xa0000000U
#define OCTEON_CIU3_ISC_CTL_IDT     0x0000000000ff0000ULL
#define OCTEON_CIU3_ISC_CTL_IDT_SHIFT 16
#define OCTEON_CIU3_ISC_CTL_IMP     0x0000000000008000ULL
#define OCTEON_CIU3_ISC_CTL_EN      0x0000000000000002ULL
#define OCTEON_CIU3_ISC_CTL_RAW     0x0000000000000001ULL
#define OCTEON_CIU3_SRC_LEVEL       0x00000001U
#define OCTEON_CIU3_SRC_RAW         0x00000002U
#define OCTEON_CIU3_NINTSN          (1U << 20)
#define OCTEON_CIU3_ISC_SIZE        (OCTEON_CIU3_NINTSN * 8)
#define OCTEON_CIU3_IDT_COUNT       (OCTEON_MAX_CPUS * 4)
#define OCTEON_CIU3_PEM_INTSN_INTA(port) (((0xc0 + (port)) << 12) + 60)
#define OCTEON_CIU3_MBOX_INTSN(cpu) ((cpu) + 0x4000U)
#define OCTEON_CIU3_UART0_INTSN     0x8000
#define OCTEON_CSR_BASE             0x1180000000000ULL
#define OCTEON_CSR_SIZE             0x100000000ULL
#define OCTEON_PEXP_BASE            0x11f0000000000ULL
#define OCTEON_PEXP_SIZE            0x40000
#define OCTEON_DPI_BASE             0x1df0000000000ULL
#define OCTEON_DPI_SIZE             0x10000
#define OCTEON_MIO_BOOT_REG_CFGX(x) \
    (0x0000000 + (x) * OCTEON_CSR_64BIT_SIZE)
#define OCTEON_MIO_BOOT_REG_CFG_INDEX(reg) \
    ((reg) / OCTEON_CSR_64BIT_SIZE)
#define OCTEON_MIO_BOOT_REG_CFG_COUNT 8
#define OCTEON_MIO_BOOT_REG_CFG_BASE 0x000000000000ffffULL
#define OCTEON_MIO_BOOT_LOC_CFGX(x) \
    (0x0000080 + ((x) & 1) * OCTEON_CSR_64BIT_SIZE)
#define OCTEON_MIO_BOOT_LOC_CFG_INDEX(reg) \
    (((reg) - OCTEON_MIO_BOOT_LOC_CFGX(0)) / OCTEON_CSR_64BIT_SIZE)
#define OCTEON_MIO_BOOT_LOC_ADR     0x0000090
#define OCTEON_MIO_BOOT_LOC_DAT     0x0000098
#define OCTEON_MIO_BOOT_LOC_SIZE    0x100
#define OCTEON_MIO_BOOT_LOC_CFG_BASE 0x000000000ffffff8ULL
#define OCTEON_MIO_BOOT_LOC_CFG_BASE_SHIFT 3
#define OCTEON_MIO_BOOT_LOC_CFG_EN  0x0000000080000000ULL
#define OCTEON_MIO_BOOT_LOC_ADR_MASK 0xf8
/* Fuses are left unprogrammed; firmware only needs reads to complete. */
#define OCTEON_MIO_FUS_DAT2         0x0001410
#define OCTEON_MIO_FUS_RCMD         0x0001500
#define OCTEON_MIO_FUS_RCMD_PEND    0x0000000000001000ULL
#define OCTEON_MIO_FUS_RCMD_DAT     0x0000000000ff0000ULL
#define OCTEON_MIO_EMM_DMA_FIFO_CFG 0x0000160
#define OCTEON_MIO_EMM_DMA_FIFO_ADR 0x0000170
#define OCTEON_MIO_EMM_DMA_FIFO_CMD 0x0000178
#define OCTEON_MIO_EMM_DMA_CFG      0x0000180
#define OCTEON_MIO_EMM_DMA_ADR      0x0000188
#define OCTEON_MIO_EMM_DMA_INT      0x0000190
#define OCTEON_MIO_EMM_CFG          0x0002000
#define OCTEON_MIO_EMM_MODEX(x) \
    (0x0002008 + (x) * OCTEON_CSR_64BIT_SIZE)
#define OCTEON_MIO_EMM_MODE_COUNT   4
#define OCTEON_MIO_EMM_INT_EN_OLD   0x0002040
#define OCTEON_MIO_EMM_SWITCH       0x0002048
#define OCTEON_MIO_EMM_DMA          0x0002050
#define OCTEON_MIO_EMM_CMD          0x0002058
#define OCTEON_MIO_EMM_RSP_STS      0x0002060
#define OCTEON_MIO_EMM_RSP_LO       0x0002068
#define OCTEON_MIO_EMM_RSP_HI       0x0002070
#define OCTEON_MIO_EMM_INT          0x0002078
#define OCTEON_MIO_EMM_WDOG         0x0002088
#define OCTEON_MIO_EMM_SAMPLE       0x0002090
#define OCTEON_MIO_EMM_STS_MASK     0x0002098
#define OCTEON_MIO_EMM_RCA          0x00020a0
#define OCTEON_MIO_EMM_BUF_IDX      0x00020e0
#define OCTEON_MIO_EMM_BUF_DAT      0x00020e8
#define OCTEON_MIO_EMM_DEBUG        0x00020f8
#define OCTEON_MIO_EMM_NODE_OFFSET  0x0002000
#define OCTEON_MIO_EMM_CMD_VAL      (1ULL << 59)
#define OCTEON_MIO_EMM_DMA_VAL      (1ULL << 59)
#define OCTEON_MIO_EMM_DMA_BLOCK_CNT_SHIFT 32
#define OCTEON_MIO_EMM_DMA_BLOCK_CNT_MASK \
    (0xffffULL << OCTEON_MIO_EMM_DMA_BLOCK_CNT_SHIFT)
#define OCTEON_MIO_EMM_SWITCH_EXE   (1ULL << 59)
#define OCTEON_MIO_EMM_CMD_IDX_SHIFT 32
#define OCTEON_MIO_EMM_CMD_IDX_BITS 6
#define OCTEON_MIO_EMM_CMD_IDX_MASK \
    ((1U << OCTEON_MIO_EMM_CMD_IDX_BITS) - 1)
#define OCTEON_MIO_EMM_CMD_BUS_ID_SHIFT 60
#define OCTEON_MIO_EMM_BUS_ID_MASK  (OCTEON_MIO_EMM_MODE_COUNT - 1)
#define OCTEON_MIO_EMM_RSP_STS_CMD_DONE (1ULL << 0)
#define OCTEON_MIO_EMM_RSP_STS_CMD_IDX_SHIFT 1
#define OCTEON_MIO_EMM_RSP_STS_RSP_TIMEOUT (1ULL << 13)
#define OCTEON_MIO_EMM_RSP_STS_DMA_VAL (1ULL << 57)
#define OCTEON_MIO_EMM_RSP_STS_DMA_PEND (1ULL << 56)
#define OCTEON_MIO_EMM_RSP_STS_BUS_ID_SHIFT 60
#define OCTEON_MIO_EMM_DMA_INT_DONE (1ULL << 0)
#define OCTEON_MIO_EMM_INT_CMD_DONE (1ULL << 1)
#define OCTEON_MIO_EMM_INT_CMD_ERR  (1ULL << 3)
#define OCTEON_MIO_EMM_INT_DMA_DONE (1ULL << 2)
#define OCTEON_MIO_EMM_INT_DMA_ERR  (1ULL << 4)
#define OCTEON_MIO_EMM_INT_SWITCH_DONE (1ULL << 5)
#define OCTEON_MIO_EMM_INT_SWITCH_ERR  (1ULL << 6)
#define OCTEON_MIO_EMM_SWITCH_MODE_MASK ((1ULL << 49) - 1)
#define OCTEON_MIO_EMM_CFG_BUS_EN_MASK \
    ((1ULL << OCTEON_MIO_EMM_MODE_COUNT) - 1)
#define OCTEON_MIO_EMM_LOW_SPEED_HZ    400000
#define OCTEON_MIO_EMM_MODE_CLK_LO_MASK 0x000000000000ffffULL
#define OCTEON_MIO_EMM_MODE_CLK_HI_MASK 0x00000000ffff0000ULL
#define OCTEON_MIO_EMM_MODE_CLK_HI_SHIFT 16
#define OCTEON_MIO_EMM_BUF_IDX_INC  (1ULL << 16)
#define OCTEON_MIO_EMM_BUF_WORD_SIZE OCTEON_CSR_64BIT_SIZE
#define OCTEON_MIO_EMM_BUF_SIZE     512
#define OCTEON_MIO_EMM_BUF_ENTRY_COUNT \
    (OCTEON_MIO_EMM_BUF_SIZE / OCTEON_MIO_EMM_BUF_WORD_SIZE)
#define OCTEON_MIO_EMM_BUF_IDX_OFF_MASK \
    (OCTEON_MIO_EMM_BUF_ENTRY_COUNT - 1)
#define OCTEON_MMC_CMD_GO_IDLE_STATE 0
#define OCTEON_MMC_CMD_BUSTEST_R    14
#define OCTEON_MMC_CMD_BUSTEST_W    19
/* SD/MMC CMD19 request patterns and CMD14 response patterns. */
#define OCTEON_MMC_BUSTEST_8_REQ0   0x55
#define OCTEON_MMC_BUSTEST_8_REQ1   0xaa
#define OCTEON_MMC_BUSTEST_8_RESP0  0xaa
#define OCTEON_MMC_BUSTEST_8_RESP1  0x55
#define OCTEON_MMC_BUSTEST_4_REQ0   0x5a
#define OCTEON_MMC_BUSTEST_4_REQ4   0x99
#define OCTEON_MMC_BUSTEST_4_REQ5   0x50
#define OCTEON_MMC_BUSTEST_4_REQ6   0x0f
#define OCTEON_MMC_BUSTEST_4_RESP0  0xa5
#define OCTEON_MMC_BUSTEST_1_REQ0   0x80
#define OCTEON_MMC_BUSTEST_1_REQ1   0x70
#define OCTEON_MMC_BUSTEST_1_REQ2   0x78
#define OCTEON_MMC_BUSTEST_1_REQ3   0x01
#define OCTEON_MMC_BUSTEST_1_RESP0  0x40
#define OCTEON_RST_BOOT             0x6001600
#define OCTEON_RST_SOFT_RST         0x6001680
/* U-Boot uses the reset controller to park and release secondary cores. */
#define OCTEON_RST_PP_POWER         0x6001700
#define OCTEON_LMC_BASE             0x88000000
#define OCTEON_LMC_SEQ_CTL          0x48
#define OCTEON_LMC_SEQ_COMPLETE     (1ULL << 5)
#define OCTEON_LMC_MPR_DATA0        0x70
#define OCTEON_LMC_MPR_DATA1        0x78
#define OCTEON_LMC_DCLK_CNT         0x1e0
#define OCTEON_LMC_PHY_CTL          0x210
#define OCTEON_LMC_PHY_DSK_COMPLETE (1ULL << 49)
#define OCTEON_LMC_RLEVEL_RANK      0x280
#define OCTEON_LMC_RLEVEL_STATUS    (3ULL << 54)
#define OCTEON_LMC_RLEVEL_DBG       0x2a8
#define OCTEON_LMC_RLEVEL_MASK      0x3f0
#define OCTEON_LMC_WLEVEL_RANK      0x2c0
#define OCTEON_LMC_RANK_COUNT       4
#define OCTEON_LMC_WLEVEL_STATUS    (3ULL << 45)
#define OCTEON_LMC_WLEVEL_DBG       0x308
#define OCTEON_LMC_WLEVEL_MASK      (0x0fULL << 4)
#define OCTEON_LMC_COUNT            4
#define OCTEON_LMC_STRIDE           0x1000000
#define OCTEON_RST_BOOT_C_MUL_SHIFT 30
#define OCTEON_RST_BOOT_PNR_MUL_SHIFT 24

#define OCTEON_FPA_BASE             0x1280000000000ULL
#define OCTEON_FPA_SIZE             0x1000
#define OCTEON_FPA_CLK_COUNT        0xf0

#define OCTEON_IPD_BASE             0x14f0000000000ULL
#define OCTEON_IPD_SIZE             0x1000
#define OCTEON_IPD_CLK_COUNT        0x338

#define OCTEON_PKO_BASE             0x1540000000000ULL
#define OCTEON_PKO_SIZE             0x1000000

#define OCTEON_RNG_BASE             0x1400000000000ULL
#define OCTEON_RNG_SIZE             0x8

#define OCTEON_POW_BASE             0x1670000000000ULL
#define OCTEON_POW_SIZE             0x2000

#define OCTEON_USB_COUNT            2
#define OCTEON_UCTL_SIZE            0x100
#define OCTEON_USB0_UCTL_BASE       0x1180068000000ULL
#define OCTEON_USB1_UCTL_BASE       0x1180069000000ULL
#define OCTEON_USB0_DWC3_BASE       0x1680000000000ULL
#define OCTEON_USB1_DWC3_BASE       0x1690000000000ULL
#define OCTEON_UCTL_SHIM_CFG        0xe8
#define OCTEON_UCTL_SHIM_CFG_CSR_BYTE_SWAP 0x3
#define OCTEON_UCTL_SHIM_CFG_CSR_NATIVE    0x3
#define OCTEON_CIU3_USB0_INTSN      0x68080
#define OCTEON_CIU3_USB1_INTSN      0x69080

#define OCTEON_TWSI_COUNT           2
#define OCTEON_TWSI_SIZE            0x200
#define OCTEON_TWSI0_BASE           0x1180000001000ULL
#define OCTEON_TWSI1_BASE           0x1180000001200ULL
#define OCTEON_TWSI_SW_TWSI         0x00
#define OCTEON_TWSI_INT             0x10
#define OCTEON_TWSI_SW_DATA_MASK    0xffffffffULL
#define OCTEON_TWSI_SW_EOP_IA_SHIFT 32
#define OCTEON_TWSI_SW_R            (1ULL << 56)
#define OCTEON_TWSI_SW_OP_SHIFT     57
#define OCTEON_TWSI_SW_V            (1ULL << 63)
#define OCTEON_TWSI_OP_EOP_IA       6
#define OCTEON_TWSI_EOP_DATA        1
#define OCTEON_TWSI_EOP_CTL         2
#define OCTEON_TWSI_EOP_STAT        3
#define OCTEON_TWSI_CTL_AAK         0x04
#define OCTEON_TWSI_CTL_IFLG        0x08
#define OCTEON_TWSI_CTL_STP         0x10
#define OCTEON_TWSI_CTL_STA         0x20
#define OCTEON_TWSI_STAT_START      0x08
#define OCTEON_TWSI_STAT_TXADDR_ACK 0x18
#define OCTEON_TWSI_STAT_TXADDR_NAK 0x20
#define OCTEON_TWSI_STAT_TXDATA_ACK 0x28
#define OCTEON_TWSI_STAT_TXDATA_NAK 0x30
#define OCTEON_TWSI_STAT_RXADDR_ACK 0x40
#define OCTEON_TWSI_STAT_RXADDR_NAK 0x48
#define OCTEON_TWSI_STAT_RXDATA_ACK 0x50
#define OCTEON_TWSI_STAT_RXDATA_NAK 0x58
#define OCTEON_TWSI_STAT_IDLE       0xf8
#define OCTEON_SPD_EEPROM_SIZE      139
#define OCTEON_SPD_EEPROM_COUNT     1
#define OCTEON_SPD_BUS              1
#define OCTEON_SPD_ADDR             0x50
#define OCTEON_SPD_MIN_ROW_BITS     12
#define OCTEON_SPD_MAX_ROW_BITS     18
#define OCTEON_SPD_MIN_COL_BITS     9
#define OCTEON_SPD_MAX_COL_BITS     10
#define OCTEON_SPD_BANK_BITS        4
#define OCTEON_SPD_MIN_RAM_SIZE     \
    (1ULL << (OCTEON_SPD_MIN_ROW_BITS + OCTEON_SPD_MIN_COL_BITS + \
              OCTEON_SPD_BANK_BITS + 3))
#define OCTEON_SPD_MAX_RAM_SIZE     \
    (1ULL << (OCTEON_SPD_MAX_ROW_BITS + OCTEON_SPD_MAX_COL_BITS + \
              OCTEON_SPD_BANK_BITS + 3))
#define OCTEON_SPD_DDR4_TCKMIN      18
#define OCTEON_SPD_DDR4_FINE_TCKMIN 125
#define OCTEON_SPD_DDR4_MTB_PS      125
#define OCTEON_SPD_DDR4_FTB_PS      1

#define OCTEON_UART0_BASE           0x1180000000800ULL
#define OCTEON_UART0_ALIAS_BASE     0x1180000000c00ULL
#define OCTEON_UART_TX_REG          0x40
#define OCTEON_UART_TX_HI32_OFFSET  OCTEON_CSR_HI32_OFFSET
#define OCTEON_UART_TX_SIZE         OCTEON_CSR_64BIT_SIZE

/*
 * QEMU-only backing for per-core CVMSEG. Keep it outside guest DRAM;
 * the guest sees the virtual CVMSEG window, not this physical address.
 */
#define OCTEON_CVMSEG_BASE          0x10000000000ULL
#define OCTEON_CVMSEG_SIZE          0x4000
#define OCTEON_CVMSEG_TOTAL_SIZE    (OCTEON_MAX_CPUS * OCTEON_CVMSEG_SIZE)

#define OCTEON_PCIE_CFG_BASE        0x1190400000000ULL
#define OCTEON_PCIE_CFG_SIZE        0x20000000ULL
#define OCTEON_PCIE_SLI_CFG_BASE    0x1190c00000000ULL
#define OCTEON_PCIE_SLI_CFG_PORTS   4
#define OCTEON_PCIE_SLI_CFG_PORT_SIZE (1ULL << 32)
#define OCTEON_PCIE_SLI_CFG_SIZE    \
    (OCTEON_PCIE_SLI_CFG_PORTS * OCTEON_PCIE_SLI_CFG_PORT_SIZE)
#define OCTEON_PCIE0_IO_BASE        0x11a0400000000ULL
#define OCTEON_PCIE0_IO_SIZE        (1ULL << 32)
#define OCTEON_PCIE0_MEM_BASE       0x11b0000000000ULL
#define OCTEON_PCIE0_MEM_SIZE       (1ULL << 32)
#define OCTEON_SLI_MEM_ACCESS_SUBID_BASE   0x000000e0
#define OCTEON_SLI_MEM_ACCESS_SUBID_STRIDE 16
#define OCTEON_SLI_MEM_ACCESS_SUBID_ESR_M  0x0000003000000000ULL
#define OCTEON_SLI_MEM_ACCESS_SUBID_ESR_S  36
#define OCTEON_SLI_MEM_ACCESS_SUBID_ESW_M  0x0000000c00000000ULL
#define OCTEON_SLI_MEM_ACCESS_SUBID_ESW_S  34
#define OCTEON_SLI_MEM_ACCESS_SWAP         3
#define OCTEON_PCIE0_ENDPOINT_DEVFN PCI_DEVFN(0x0b, 0)

#define OCTEON_GSERX_CFG0           0x90000080
#define OCTEON_GSERX_QLM_STAT0      0x900000a0
#define OCTEON_RST_CTLX0            0x6001640
#define OCTEON_RST_SOFT_PRSTX0      0x60016c0
#define OCTEON_PEMX_CFG_RD0         0xc0000030
#define OCTEON_PEMX_CFG_WR0         0xc0000028
#define OCTEON_PEMX_BIST_STATUS0    0xc0000440
#define OCTEON_PEMX_ON0             0xc0000420
#define OCTEON_PEMX_STRAP0          0xc0000408

#define OCTEON_RST_CTL_HOST_MODE    (1ULL << 6)
#define OCTEON_RST_CTL_RST_DONE     (1ULL << 8)
#define OCTEON_PCIE_CFG006          0x018
#define OCTEON_PCIE_CFG031          0x07c
#define OCTEON_PCIE_CFG032          0x080
#define OCTEON_PCIE_CFG068          0x110
#define OCTEON_PCIE_CFG_STORE_BASE  (1ULL << 63)

#define OCTEON_FLASH_BASE           0x1f400000ULL
#define OCTEON_FLASH_RESET_BASE     0x1fc00000ULL
#define OCTEON_FLASH_RESET_SIZE     (4 * MiB)
#define OCTEON_FLASH_MAIN_SECTORS   127
#define OCTEON_FLASH_MAIN_SECTOR_SIZE (64 * KiB)
#define OCTEON_FLASH_ENV_SECTORS    8
#define OCTEON_FLASH_ENV_SECTOR_SIZE 0x2000
#define OCTEON_FLASH_SIZE           \
    (OCTEON_FLASH_MAIN_SECTORS * OCTEON_FLASH_MAIN_SECTOR_SIZE + \
     OCTEON_FLASH_ENV_SECTORS * OCTEON_FLASH_ENV_SECTOR_SIZE)
#define OCTEON_FLASH_ENV_OFFSET     \
    (OCTEON_FLASH_SIZE - OCTEON_FLASH_ENV_SECTOR_SIZE)
#define OCTEON_FLASH_WIDTH          2
#define OCTEON_MIO_BOOT_REG_CFG_RESET_BASE (OCTEON_FLASH_RESET_BASE >> 16)
#define OCTEON_BOOTBUS_LED_CS       4
#define OCTEON_BOOTBUS_LED_SIZE     (64 * KiB)

typedef enum OcteonIRQ {
    OCTEON_IRQ_UART,
    OCTEON_IRQ_PCI,
    OCTEON_IRQ_USB0,
    OCTEON_IRQ_USB1,
    OCTEON_IRQ_COUNT,
} OcteonIRQ;

typedef enum OcteonCiu3Source {
    OCTEON_CIU3_SRC_PCI_INTA,
    OCTEON_CIU3_SRC_UART0,
    OCTEON_CIU3_SRC_USB0,
    OCTEON_CIU3_SRC_USB1,
    OCTEON_CIU3_SRC_MBOX0,
    OCTEON_CIU3_SRC_COUNT = OCTEON_CIU3_SRC_MBOX0 + OCTEON_MAX_CPUS,
} OcteonCiu3Source;

typedef struct OcteonState OcteonState;

struct OcteonMachineState {
    MachineState parent_obj;
    uint64_t cpu_hz;
    uint64_t ref_hz;
    uint64_t io_hz;
    uint64_t ddr_hz;
};

struct OcteonPCIHostState {
    PCIHostState parent_obj;
    MemoryRegion mem;
    MemoryRegion io;
};

typedef struct OcteonCPUState {
    OcteonState *board;
    MIPSCPU *cpu;
    bool boot_cpu;
} OcteonCPUState;

typedef struct OcteonSpdEepromState {
    unsigned int bus;
    uint8_t addr;
    uint16_t offset;
    uint8_t data[OCTEON_SPD_EEPROM_SIZE];
} OcteonSpdEepromState;

typedef struct OcteonLmcState {
    GHashTable *regs;
} OcteonLmcState;

typedef struct OcteonTWSIState {
    OcteonState *board;
    MemoryRegion mmio;
    unsigned int bus;
    uint64_t sw_twsi;
    uint64_t int_reg;
    uint8_t ctl;
    uint8_t stat;
    uint8_t data;
    uint8_t slave_addr;
    uint8_t write_buf[2];
    unsigned int write_len;
    bool have_slave;
    bool read_transfer;
    bool addr_phase;
} OcteonTWSIState;

typedef struct OcteonUsbState {
    OcteonState *board;
    USBDWC3 *dwc3;
    unsigned int index;
    MemoryRegion uctl;
    MemoryRegion dwc3_window;
    AddressSpace dwc3_as;
    GHashTable *regs;
} OcteonUsbState;

struct OcteonState {
    MachineState *machine;
    Clock *cpuclk;
    uint64_t cpu_hz;
    uint64_t ref_hz;
    uint64_t io_hz;
    uint64_t ddr_hz;
    OcteonCPUState cpu[OCTEON_MAX_CPUS];
    unsigned int cpu_count;
    uint64_t firmware_entry;

    MemoryRegion dr0;
    MemoryRegion dr1;
    MemoryRegion *flash;
    MemoryRegion boot_flash;
    MemoryRegion boot_flash_alias;
    MemoryRegion bootbus_led;
    MemoryRegion mio_boot_loc;
    MemoryRegion cvmseg;
    MemoryRegion ciu;
    MemoryRegion ciu3;
    MemoryRegion csr;
    MemoryRegion pexp;
    MemoryRegion dpi;
    MemoryRegion fpa;
    MemoryRegion ipd;
    MemoryRegion pko;
    MemoryRegion rng;
    MemoryRegion pow;
    OcteonLmcState lmc[OCTEON_LMC_COUNT];
    OcteonTWSIState twsi[OCTEON_TWSI_COUNT];
    OcteonUsbState usb[OCTEON_USB_COUNT];
    OcteonSpdEepromState spd[OCTEON_SPD_EEPROM_COUNT];
    SerialMM *uart;
    MemoryRegion uart_alias;
    MemoryRegion uart_tx;
    MemoryRegion uart_alias_tx;
    MemoryRegion pcie_cfg;
    MemoryRegion pcie_sli_cfg;
    MemoryRegion pcie0_io;
    MemoryRegion pcie0_mem;
    AddressSpace pcie0_mem_as;

    uint64_t mio_boot_reg_cfg[OCTEON_MIO_BOOT_REG_CFG_COUNT];
    uint64_t mio_boot_loc_cfg[2];
    uint64_t mio_boot_loc_adr;
    uint64_t mio_fus_rcmd;
    uint64_t mio_emm_rsp_sts;
    uint64_t mio_emm_rsp_lo;
    uint64_t mio_emm_rsp_hi;
    uint64_t mio_emm_int;
    uint64_t mio_emm_dma_int;
    uint16_t mio_emm_buf_idx;
    bool mio_emm_buf_inc;
    uint8_t mio_emm_buf[OCTEON_MIO_EMM_BUF_SIZE];
    uint64_t rst_pp_power;
    hwaddr mio_boot_loc_base;
    uint8_t mio_boot_loc_data[OCTEON_MIO_BOOT_LOC_SIZE];
    bool mio_boot_loc_mapped;
    hwaddr bootbus_led_base;
    bool bootbus_led_mapped;
    uint32_t pcie_cfg_rd_addr;
    uint32_t ciu_mbox[OCTEON_MAX_CPUS];
    uint64_t ciu_ipi_en[OCTEON_MAX_CPUS];
    uint64_t ciu_gpio_rx;
    uint64_t ciu_gpio_tx;
    uint64_t ciu_gpio_bit_cfg[OCTEON_CIU_GPIO_COUNT];
    bool uart_pending;
    bool pci_pending;
    uint64_t ciu3_pp_rst;
    uint32_t ciu3_src_state[OCTEON_CIU3_SRC_COUNT];
    uint64_t ciu3_src_ctl[OCTEON_CIU3_SRC_COUNT];
    uint64_t ciu3_idt_ctl[OCTEON_CIU3_IDT_COUNT];
    uint64_t ciu3_idt_pp[OCTEON_CIU3_IDT_COUNT];
    uint64_t ciu3_idt_io[OCTEON_CIU3_IDT_COUNT];
    uint16_t ciu3_src_cursor[OCTEON_MAX_CPUS][OCTEON_CIU3_CP0_IRQ_COUNT];
    uint8_t ciu3_irq_line[OCTEON_MAX_CPUS][OCTEON_CIU3_CP0_IRQ_COUNT];

    PCIBus *pci_bus;
    GHashTable *csr_values;
};

static const uint8_t octeon_spd_eeprom_template[OCTEON_SPD_EEPROM_SIZE] = {
    [0] = 0x23, [1] = 0x11, [2] = 0x0c, [3] = 0x02,
    [4] = 0x82, [5] = 0x19, [11] = 0x03, [12] = 0x01,
    [13] = 0x03, [18] = 0x0a, [19] = 0x20, [20] = 0x78,
    [24] = 0x6e, [25] = 0x6e, [26] = 0x6e, [27] = 0x11,
    [29] = 0x6e, [31] = 0x05, [32] = 0x70, [33] = 0x03,
    [34] = 0xd0, [35] = 0x02, [37] = 0xa8, [38] = 0x20,
    [39] = 0x27, [40] = 0x28, [126] = 0x16, [127] = 0x2f,
};

static guint octeon_uint64_hash(gconstpointer v)
{
    uint64_t value = *(const uint64_t *)v;

    return value ^ (value >> 32);
}

static gboolean octeon_uint64_equal(gconstpointer a, gconstpointer b)
{
    return *(const uint64_t *)a == *(const uint64_t *)b;
}

static uint64_t octeon_read64(uint64_t value, hwaddr addr, unsigned size)
{
    if (size == 4) {
        if (addr & 4) {
            return value & 0xffffffffU;
        }
        return value >> 32;
    }

    return value;
}

static uint64_t octeon_write64(uint64_t old, hwaddr addr,
                               uint64_t value, unsigned size)
{
    if (size == 4) {
        if (addr & 4) {
            return (old & 0xffffffff00000000ULL) | (value & 0xffffffffU);
        }
        return (old & 0xffffffffULL) | ((value & 0xffffffffU) << 32);
    }

    return value;
}

static uint64_t octeon_atomic_write64(uint64_t *ptr, hwaddr addr,
                                      uint64_t value, unsigned size,
                                      uint64_t and_mask, uint64_t or_mask,
                                      uint64_t *oldp)
{
    uint64_t old;
    uint64_t new;
    uint64_t cmp = qatomic_read(ptr);

    do {
        old = cmp;
        new = (octeon_write64(old, addr, value, size) & and_mask) | or_mask;
        cmp = qatomic_cmpxchg(ptr, old, new);
    } while (old != cmp);

    if (oldp) {
        *oldp = old;
    }

    return new;
}

static uint64_t octeon_clock_count(uint64_t hz)
{
    uint64_t now = qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL);

    return muldiv64(now, hz, NANOSECONDS_PER_SECOND);
}

static uint16_t octeon_spd_crc16(const uint8_t *ptr, int count)
{
    int crc = 0;
    int i;

    while (--count >= 0) {
        crc ^= *ptr++ << 8;
        for (i = 0; i < 8; i++) {
            if (crc & 0x8000) {
                crc = (crc << 1) ^ 0x1021;
            } else {
                crc <<= 1;
            }
        }
    }

    return crc & 0xffff;
}

static bool octeon_spd_geometry(uint64_t ram_size, int *row_bits,
                                int *col_bits, int *density_code)
{
    uint64_t ram_mb;
    int pbank_lsb;
    int row_col_bits;
    int col;
    int row;
    int density;

    if (ram_size < OCTEON_SPD_MIN_RAM_SIZE ||
        ram_size > OCTEON_SPD_MAX_RAM_SIZE ||
        !is_power_of_2(ram_size)) {
        return false;
    }

    ram_mb = ram_size / MiB;
    pbank_lsb = ctz64(ram_size);
    row_col_bits = pbank_lsb - 3 - OCTEON_SPD_BANK_BITS;

    col = MIN(OCTEON_SPD_MAX_COL_BITS,
              row_col_bits - OCTEON_SPD_MIN_ROW_BITS);
    col = MAX(OCTEON_SPD_MIN_COL_BITS, col);
    row = row_col_bits - col;
    density = ctz64(ram_mb) - 8;

    if (row < OCTEON_SPD_MIN_ROW_BITS ||
        row > OCTEON_SPD_MAX_ROW_BITS ||
        density < 0 || density > 7) {
        return false;
    }

    if (row_bits) {
        *row_bits = row;
    }
    if (col_bits) {
        *col_bits = col;
    }
    if (density_code) {
        *density_code = density;
    }
    return true;
}

static void octeon_validate_ram_size(uint64_t ram_size)
{
    if (!octeon_spd_geometry(ram_size, NULL, NULL, NULL)) {
        error_report("octeon3 RAM size must be a power of two from "
                     "%" PRIu64 " MB to %" PRIu64 " GB",
                     (uint64_t)OCTEON_SPD_MIN_RAM_SIZE / MiB,
                     (uint64_t)OCTEON_SPD_MAX_RAM_SIZE / GiB);
        exit(1);
    }
}

static void octeon_validate_clocks(OcteonMachineState *oms)
{
    if (!oms->cpu_hz || !oms->ref_hz || !oms->io_hz || !oms->ddr_hz) {
        error_report("octeon3 clock frequencies must be non-zero");
        exit(1);
    }

    if (oms->cpu_hz % oms->ref_hz || oms->io_hz % oms->ref_hz) {
        error_report("octeon3 CPU and IO clocks must be multiples of "
                     "ref-clock-hz");
        exit(1);
    }
}

static void octeon_spd_set_tckmin(OcteonState *s, uint8_t *spd)
{
    uint64_t tck_ps = (1000000000000ULL + s->ddr_hz - 1) / s->ddr_hz;
    uint64_t mtb = tck_ps / OCTEON_SPD_DDR4_MTB_PS;
    uint64_t fine = tck_ps % OCTEON_SPD_DDR4_MTB_PS;

    if (mtb == 0 || mtb > UINT8_MAX || fine > INT8_MAX) {
        error_report("octeon3 DDR clock cannot be represented in DDR4 SPD");
        exit(1);
    }

    spd[OCTEON_SPD_DDR4_TCKMIN] = mtb;
    spd[OCTEON_SPD_DDR4_FINE_TCKMIN] = fine / OCTEON_SPD_DDR4_FTB_PS;
}

static void octeon_init_spd(OcteonState *s)
{
    OcteonSpdEepromState *eeprom = &s->spd[0];
    uint64_t ram_size = s->machine->ram_size;
    int col_bits;
    int row_bits;
    int density_code;
    uint16_t crc;

    eeprom->bus = OCTEON_SPD_BUS;
    eeprom->addr = OCTEON_SPD_ADDR;
    memcpy(eeprom->data, octeon_spd_eeprom_template,
           sizeof(eeprom->data));

    g_assert(octeon_spd_geometry(ram_size, &row_bits, &col_bits,
                                 &density_code));

    eeprom->data[4] = 0x80 | density_code;
    eeprom->data[5] = ((row_bits - OCTEON_SPD_MIN_ROW_BITS) << 3) |
                      (col_bits - OCTEON_SPD_MIN_COL_BITS);
    octeon_spd_set_tckmin(s, eeprom->data);

    crc = octeon_spd_crc16(eeprom->data,
                           (eeprom->data[0] & 0x80) ? 117 : 126);
    eeprom->data[126] = crc & 0xff;
    eeprom->data[127] = crc >> 8;
}

static bool octeon_reg_lookup(GHashTable *regs, uint64_t reg, uint64_t *value)
{
    uint64_t *stored = g_hash_table_lookup(regs, &reg);

    if (!stored) {
        return false;
    }

    *value = *stored;
    return true;
}

static void octeon_reg_store(GHashTable *regs, uint64_t reg, uint64_t value)
{
    uint64_t *key = g_new(uint64_t, 1);
    uint64_t *stored = g_new(uint64_t, 1);

    *key = reg;
    *stored = value;
    g_hash_table_replace(regs, key, stored);
}

static bool octeon_csr_lookup(OcteonState *s, uint64_t reg, uint64_t *value)
{
    return octeon_reg_lookup(s->csr_values, reg, value);
}

static void octeon_csr_store(OcteonState *s, uint64_t reg, uint64_t value)
{
    octeon_reg_store(s->csr_values, reg, value);
}

static hwaddr octeon_mio_boot_loc_base(uint64_t cfg)
{
    uint64_t base = (cfg & OCTEON_MIO_BOOT_LOC_CFG_BASE) >>
                    OCTEON_MIO_BOOT_LOC_CFG_BASE_SHIFT;

    return base << 7;
}

static void octeon_mio_boot_loc_update(OcteonState *s)
{
    MemoryRegion *sysmem = get_system_memory();
    bool enabled = s->mio_boot_loc_cfg[0] & OCTEON_MIO_BOOT_LOC_CFG_EN;
    hwaddr base = octeon_mio_boot_loc_base(s->mio_boot_loc_cfg[0]);

    if (s->mio_boot_loc_mapped) {
        if (enabled && base == s->mio_boot_loc_base) {
            return;
        }
        memory_region_del_subregion(sysmem, &s->mio_boot_loc);
        s->mio_boot_loc_mapped = false;
    }

    if (enabled) {
        memory_region_add_subregion_overlap(sysmem, base,
                                            &s->mio_boot_loc, 1);
        s->mio_boot_loc_base = base;
        s->mio_boot_loc_mapped = true;
    }
}

static hwaddr octeon_mio_boot_reg_cfg_base(uint64_t cfg)
{
    return (cfg & OCTEON_MIO_BOOT_REG_CFG_BASE) << 16;
}

static void octeon_bootbus_led_update(OcteonState *s)
{
    MemoryRegion *sysmem = get_system_memory();
    hwaddr base = octeon_mio_boot_reg_cfg_base(
        s->mio_boot_reg_cfg[OCTEON_BOOTBUS_LED_CS]);

    if (s->bootbus_led_mapped) {
        if (base == s->bootbus_led_base) {
            return;
        }
        memory_region_del_subregion(sysmem, &s->bootbus_led);
        s->bootbus_led_mapped = false;
    }

    if (base) {
        memory_region_add_subregion(sysmem, base, &s->bootbus_led);
        s->bootbus_led_base = base;
        s->bootbus_led_mapped = true;
    }
}

static uint64_t octeon_mio_boot_loc_dat_read(OcteonState *s)
{
    unsigned int offset = s->mio_boot_loc_adr & OCTEON_MIO_BOOT_LOC_ADR_MASK;
    uint64_t value = ldq_be_p(s->mio_boot_loc_data + offset);

    s->mio_boot_loc_adr =
        (s->mio_boot_loc_adr + OCTEON_CSR_64BIT_SIZE) &
        OCTEON_MIO_BOOT_LOC_ADR_MASK;
    return value;
}

static void octeon_mio_boot_loc_dat_write(OcteonState *s, hwaddr addr,
                                          uint64_t value, unsigned size)
{
    unsigned int offset = s->mio_boot_loc_adr & OCTEON_MIO_BOOT_LOC_ADR_MASK;
    uint64_t old = ldq_be_p(s->mio_boot_loc_data + offset);

    value = octeon_write64(old, addr, value, size);
    stq_be_p(s->mio_boot_loc_data + offset, value);
    memory_region_set_dirty(&s->mio_boot_loc, offset, sizeof(value));
    s->mio_boot_loc_adr =
        (s->mio_boot_loc_adr + OCTEON_CSR_64BIT_SIZE) &
        OCTEON_MIO_BOOT_LOC_ADR_MASK;
}

static bool octeon_mio_emm_valid_reg(hwaddr reg)
{
    if (reg >= OCTEON_MIO_EMM_MODEX(0) &&
        reg < OCTEON_MIO_EMM_MODEX(OCTEON_MIO_EMM_MODE_COUNT) &&
        (reg & (OCTEON_CSR_64BIT_SIZE - 1)) == 0) {
        return true;
    }

    switch (reg) {
    case OCTEON_MIO_EMM_DMA_INT:
    case OCTEON_MIO_EMM_CFG:
    case OCTEON_MIO_EMM_SWITCH:
    case OCTEON_MIO_EMM_DMA:
    case OCTEON_MIO_EMM_CMD:
    case OCTEON_MIO_EMM_RSP_STS:
    case OCTEON_MIO_EMM_INT:
    case OCTEON_MIO_EMM_DMA_FIFO_CFG:
    case OCTEON_MIO_EMM_DMA_FIFO_ADR:
    case OCTEON_MIO_EMM_DMA_FIFO_CMD:
    case OCTEON_MIO_EMM_DMA_CFG:
    case OCTEON_MIO_EMM_DMA_ADR:
    case OCTEON_MIO_EMM_INT_EN_OLD:
    case OCTEON_MIO_EMM_RSP_LO:
    case OCTEON_MIO_EMM_RSP_HI:
    case OCTEON_MIO_EMM_WDOG:
    case OCTEON_MIO_EMM_SAMPLE:
    case OCTEON_MIO_EMM_STS_MASK:
    case OCTEON_MIO_EMM_RCA:
    case OCTEON_MIO_EMM_BUF_IDX:
    case OCTEON_MIO_EMM_BUF_DAT:
    case OCTEON_MIO_EMM_DEBUG:
        return true;
    default:
        return false;
    }
}

static bool octeon_mio_emm_decode(hwaddr reg, hwaddr *ereg)
{
    if (octeon_mio_emm_valid_reg(reg)) {
        *ereg = reg;
        return true;
    }

    reg -= OCTEON_MIO_EMM_NODE_OFFSET;
    if (octeon_mio_emm_valid_reg(reg)) {
        *ereg = reg;
        return true;
    }

    return false;
}

static bool octeon_mio_emm_execute_write(hwaddr addr, unsigned size)
{
    return size == OCTEON_CSR_64BIT_SIZE ||
           (size == OCTEON_CSR_32BIT_SIZE &&
            (addr & OCTEON_CSR_LO32_OFFSET));
}

static void octeon_mio_emm_set_low_speed_mode(OcteonState *s,
                                               unsigned int bus_id)
{
    uint64_t period = DIV_ROUND_UP(s->io_hz, OCTEON_MIO_EMM_LOW_SPEED_HZ);
    uint64_t value;

    period = MIN(period, UINT16_MAX);
    if (!octeon_csr_lookup(s, OCTEON_MIO_EMM_MODEX(bus_id), &value)) {
        value = 0;
    }

    value &= ~(OCTEON_MIO_EMM_MODE_CLK_LO_MASK |
               OCTEON_MIO_EMM_MODE_CLK_HI_MASK);
    value |= period | (period << OCTEON_MIO_EMM_MODE_CLK_HI_SHIFT);
    octeon_csr_store(s, OCTEON_MIO_EMM_MODEX(bus_id), value);
}

static void octeon_mio_emm_set_bustest(OcteonState *s)
{
    const uint8_t *buf = s->mio_emm_buf;
    bool bus8 = buf[0] == OCTEON_MMC_BUSTEST_8_REQ0 &&
                buf[1] == OCTEON_MMC_BUSTEST_8_REQ1;
    bool bus4 = buf[0] == OCTEON_MMC_BUSTEST_4_REQ0 &&
                buf[4] == OCTEON_MMC_BUSTEST_4_REQ4 &&
                buf[5] == OCTEON_MMC_BUSTEST_4_REQ5 &&
                buf[6] == OCTEON_MMC_BUSTEST_4_REQ6;
    bool bus1 = buf[0] == OCTEON_MMC_BUSTEST_1_REQ0 &&
                buf[1] == OCTEON_MMC_BUSTEST_1_REQ1 &&
                buf[2] == OCTEON_MMC_BUSTEST_1_REQ2 &&
                buf[3] == OCTEON_MMC_BUSTEST_1_REQ3;

    memset(s->mio_emm_buf, 0, sizeof(s->mio_emm_buf));
    if (bus8) {
        s->mio_emm_buf[0] = OCTEON_MMC_BUSTEST_8_RESP0;
        s->mio_emm_buf[1] = OCTEON_MMC_BUSTEST_8_RESP1;
    } else if (bus4) {
        s->mio_emm_buf[0] = OCTEON_MMC_BUSTEST_4_RESP0;
    } else if (bus1) {
        s->mio_emm_buf[0] = OCTEON_MMC_BUSTEST_1_RESP0;
    }
}

static void octeon_mio_emm_complete_dma(OcteonState *s, uint64_t dma)
{
    dma &= ~(OCTEON_MIO_EMM_DMA_VAL | OCTEON_MIO_EMM_DMA_BLOCK_CNT_MASK);
    octeon_csr_store(s, OCTEON_MIO_EMM_DMA, dma);
    s->mio_emm_rsp_sts &= ~(OCTEON_MIO_EMM_RSP_STS_DMA_VAL |
                            OCTEON_MIO_EMM_RSP_STS_DMA_PEND);
    s->mio_emm_dma_int |= OCTEON_MIO_EMM_DMA_INT_DONE;
    s->mio_emm_int |= OCTEON_MIO_EMM_INT_DMA_DONE;
}

static void octeon_mio_emm_command(OcteonState *s, uint64_t cmd)
{
    unsigned int cmd_idx = (cmd >> OCTEON_MIO_EMM_CMD_IDX_SHIFT) &
                           OCTEON_MIO_EMM_CMD_IDX_MASK;
    unsigned int bus_id = (cmd >> OCTEON_MIO_EMM_CMD_BUS_ID_SHIFT) &
                          OCTEON_MIO_EMM_BUS_ID_MASK;

    s->mio_emm_rsp_lo = 0;
    s->mio_emm_rsp_hi = 0;
    s->mio_emm_rsp_sts =
        OCTEON_MIO_EMM_RSP_STS_CMD_DONE |
        ((uint64_t)cmd_idx << OCTEON_MIO_EMM_RSP_STS_CMD_IDX_SHIFT) |
        ((uint64_t)bus_id << OCTEON_MIO_EMM_RSP_STS_BUS_ID_SHIFT);

    switch (cmd_idx) {
    case OCTEON_MMC_CMD_GO_IDLE_STATE:
    case OCTEON_MMC_CMD_BUSTEST_W:
        break;
    case OCTEON_MMC_CMD_BUSTEST_R:
        octeon_mio_emm_set_bustest(s);
        break;
    default:
        s->mio_emm_rsp_sts |= OCTEON_MIO_EMM_RSP_STS_RSP_TIMEOUT;
        break;
    }

    s->mio_emm_int |= OCTEON_MIO_EMM_INT_CMD_DONE;
    if (s->mio_emm_rsp_sts & OCTEON_MIO_EMM_RSP_STS_RSP_TIMEOUT) {
        s->mio_emm_int |= OCTEON_MIO_EMM_INT_CMD_ERR;
    }
}

static uint64_t octeon_mio_emm_read(OcteonState *s, hwaddr reg,
                                    hwaddr addr, unsigned size)
{
    uint64_t value;

    switch (reg) {
    case OCTEON_MIO_EMM_RSP_STS:
        value = s->mio_emm_rsp_sts;
        break;
    case OCTEON_MIO_EMM_RSP_LO:
        value = s->mio_emm_rsp_lo;
        break;
    case OCTEON_MIO_EMM_RSP_HI:
        value = s->mio_emm_rsp_hi;
        break;
    case OCTEON_MIO_EMM_BUF_DAT:
        value = ldq_be_p(s->mio_emm_buf + s->mio_emm_buf_idx);
        if (s->mio_emm_buf_inc) {
            s->mio_emm_buf_idx =
                (s->mio_emm_buf_idx + OCTEON_MIO_EMM_BUF_WORD_SIZE) %
                OCTEON_MIO_EMM_BUF_SIZE;
        }
        break;
    case OCTEON_MIO_EMM_INT:
        value = s->mio_emm_int;
        break;
    case OCTEON_MIO_EMM_DMA_INT:
        value = s->mio_emm_dma_int;
        break;
    case OCTEON_MIO_EMM_CMD:
    case OCTEON_MIO_EMM_SWITCH:
    case OCTEON_MIO_EMM_DMA:
        if (!octeon_csr_lookup(s, reg, &value)) {
            value = 0;
        }
        break;
    case OCTEON_MIO_EMM_MODEX(0):
    case OCTEON_MIO_EMM_MODEX(1):
    case OCTEON_MIO_EMM_MODEX(2):
    case OCTEON_MIO_EMM_MODEX(3):
        if (!octeon_csr_lookup(s, reg, &value)) {
            value = 0;
        }
        break;
    default:
        if (!octeon_csr_lookup(s, reg, &value)) {
            value = 0;
        }
        break;
    }

    return octeon_read64(value, addr, size);
}

static void octeon_mio_emm_write(OcteonState *s, hwaddr reg,
                                 hwaddr addr, uint64_t value, unsigned size)
{
    uint64_t old;
    uint64_t clear;

    switch (reg) {
    case OCTEON_MIO_EMM_CMD:
        if (!octeon_csr_lookup(s, reg, &old)) {
            old = 0;
        }
        value = octeon_write64(old, addr, value, size);
        octeon_csr_store(s, reg, value);
        if (!(value & OCTEON_MIO_EMM_CMD_VAL)) {
            return;
        }
        if (!octeon_mio_emm_execute_write(addr, size)) {
            return;
        }

        octeon_csr_store(s, reg, value & ~OCTEON_MIO_EMM_CMD_VAL);
        octeon_mio_emm_command(s, value);
        return;
    case OCTEON_MIO_EMM_SWITCH:
        if (!octeon_csr_lookup(s, reg, &old)) {
            old = 0;
        }
        value = octeon_write64(old, addr, value, size);
        octeon_csr_store(s, reg, value);
        if (!(value & OCTEON_MIO_EMM_SWITCH_EXE)) {
            return;
        }
        if (!octeon_mio_emm_execute_write(addr, size)) {
            return;
        }
        octeon_csr_store(s, reg, value & ~OCTEON_MIO_EMM_SWITCH_EXE);
        octeon_csr_store(s, OCTEON_MIO_EMM_MODEX(
                         (value >> OCTEON_MIO_EMM_CMD_BUS_ID_SHIFT) &
                         OCTEON_MIO_EMM_BUS_ID_MASK),
                         value & OCTEON_MIO_EMM_SWITCH_MODE_MASK);
        s->mio_emm_int |= OCTEON_MIO_EMM_INT_SWITCH_DONE;
        return;
    case OCTEON_MIO_EMM_DMA:
        if (!octeon_csr_lookup(s, reg, &old)) {
            old = 0;
        }
        value = octeon_write64(old, addr, value, size);
        octeon_csr_store(s, reg, value);
        if (!(value & OCTEON_MIO_EMM_DMA_VAL)) {
            return;
        }
        if (!octeon_mio_emm_execute_write(addr, size)) {
            return;
        }
        octeon_mio_emm_complete_dma(s, value);
        return;
    case OCTEON_MIO_EMM_INT:
        clear = octeon_write64(0, addr, value, size);
        s->mio_emm_int &= ~clear;
        return;
    case OCTEON_MIO_EMM_DMA_INT:
        clear = octeon_write64(0, addr, value, size);
        s->mio_emm_dma_int &= ~clear;
        return;
    case OCTEON_MIO_EMM_MODEX(0):
    case OCTEON_MIO_EMM_MODEX(1):
    case OCTEON_MIO_EMM_MODEX(2):
    case OCTEON_MIO_EMM_MODEX(3):
        if (!octeon_csr_lookup(s, reg, &old)) {
            old = 0;
        }
        value = octeon_write64(old, addr, value, size);
        octeon_csr_store(s, reg, value);
        return;
    case OCTEON_MIO_EMM_CFG:
        if (!octeon_csr_lookup(s, reg, &old)) {
            old = 0;
        }
        value = octeon_write64(old, addr, value, size);
        octeon_csr_store(s, reg, value);
        if (octeon_mio_emm_execute_write(addr, size)) {
            uint64_t enabled = (value & ~old) & OCTEON_MIO_EMM_CFG_BUS_EN_MASK;
            unsigned int i;

            for (i = 0; i < OCTEON_MIO_EMM_MODE_COUNT; i++) {
                if (enabled & (1U << i)) {
                    octeon_mio_emm_set_low_speed_mode(s, i);
                }
            }
        }
        return;
    case OCTEON_MIO_EMM_BUF_IDX:
        if (!octeon_csr_lookup(s, reg, &old)) {
            old = 0;
        }
        value = octeon_write64(old, addr, value, size);
        octeon_csr_store(s, reg, value);
        s->mio_emm_buf_idx = (value & OCTEON_MIO_EMM_BUF_IDX_OFF_MASK) *
                             OCTEON_MIO_EMM_BUF_WORD_SIZE;
        s->mio_emm_buf_inc = value & OCTEON_MIO_EMM_BUF_IDX_INC;
        return;
    case OCTEON_MIO_EMM_BUF_DAT:
        old = ldq_be_p(s->mio_emm_buf + s->mio_emm_buf_idx);
        value = octeon_write64(old, addr, value, size);
        stq_be_p(s->mio_emm_buf + s->mio_emm_buf_idx, value);
        if (s->mio_emm_buf_inc) {
            s->mio_emm_buf_idx =
                (s->mio_emm_buf_idx + OCTEON_MIO_EMM_BUF_WORD_SIZE) %
                OCTEON_MIO_EMM_BUF_SIZE;
        }
        return;
    default:
        if (!octeon_csr_lookup(s, reg, &old)) {
            old = 0;
        }
        value = octeon_write64(old, addr, value, size);
        octeon_csr_store(s, reg, value);
        return;
    }
}

static uint32_t octeon_pcie_root_cfg_default(uint32_t reg)
{
    switch (reg) {
    case OCTEON_PCIE_CFG006:
        return 0x00010101;
    case OCTEON_PCIE_CFG031:
        return 0x00000011;
    case OCTEON_PCIE_CFG032:
        return 0x20110000;
    default:
        return 0;
    }
}

static uint32_t octeon_pcie_root_cfg_read(OcteonState *s, uint32_t reg)
{
    uint64_t value;

    reg &= ~3U;
    switch (reg) {
    case OCTEON_PCIE_CFG006:
    case OCTEON_PCIE_CFG031:
    case OCTEON_PCIE_CFG032:
        return octeon_pcie_root_cfg_default(reg);
    default:
        break;
    }

    if (octeon_csr_lookup(s, OCTEON_PCIE_CFG_STORE_BASE | reg, &value)) {
        return value;
    }

    return octeon_pcie_root_cfg_default(reg);
}

static void octeon_pcie_root_cfg_write(OcteonState *s, uint32_t reg,
                                       uint32_t value)
{
    reg &= ~3U;
    if (reg == OCTEON_PCIE_CFG068) {
        value = 0;
    }

    octeon_csr_store(s, OCTEON_PCIE_CFG_STORE_BASE | reg, value);
}

static void octeon_init_lmcs(OcteonState *s)
{
    unsigned int i;

    for (i = 0; i < OCTEON_LMC_COUNT; i++) {
        s->lmc[i].regs = g_hash_table_new_full(octeon_uint64_hash,
                                                octeon_uint64_equal,
                                                g_free, g_free);
    }
}

static bool octeon_lmc_decode(hwaddr reg, OcteonLmcState **lmc, hwaddr *lreg,
                              OcteonState *s)
{
    hwaddr offset;
    unsigned int index;

    if (reg < OCTEON_LMC_BASE) {
        return false;
    }

    offset = reg - OCTEON_LMC_BASE;
    index = offset / OCTEON_LMC_STRIDE;
    if (index >= OCTEON_LMC_COUNT) {
        return false;
    }

    *lmc = &s->lmc[index];
    *lreg = offset % OCTEON_LMC_STRIDE;
    return true;
}

static uint64_t octeon_lmc_get(OcteonLmcState *lmc, hwaddr reg)
{
    uint64_t value;

    if (!octeon_reg_lookup(lmc->regs, reg, &value)) {
        return 0;
    }
    return value;
}

static bool octeon_lmc_rank_reg(hwaddr reg, hwaddr base)
{
    return reg >= base &&
           reg < base + OCTEON_LMC_RANK_COUNT * 8 &&
           ((reg - base) & 7) == 0;
}

static uint64_t octeon_lmc_read(OcteonState *s, OcteonLmcState *lmc,
                                hwaddr reg, hwaddr addr, unsigned size)
{
    uint64_t value;

    switch (reg) {
    case OCTEON_LMC_SEQ_CTL:
        value = octeon_lmc_get(lmc, reg) | OCTEON_LMC_SEQ_COMPLETE;
        break;
    case OCTEON_LMC_MPR_DATA0:
        value = ~0ULL;
        break;
    case OCTEON_LMC_MPR_DATA1:
        value = 0xff;
        break;
    case OCTEON_LMC_DCLK_CNT:
        value = octeon_clock_count(s->ddr_hz);
        break;
    case OCTEON_LMC_PHY_CTL:
        value = octeon_lmc_get(lmc, reg) | OCTEON_LMC_PHY_DSK_COMPLETE;
        break;
    case OCTEON_LMC_RLEVEL_DBG:
        value = OCTEON_LMC_RLEVEL_MASK;
        break;
    case OCTEON_LMC_WLEVEL_DBG:
        value = OCTEON_LMC_WLEVEL_MASK;
        break;
    default:
        if (octeon_lmc_rank_reg(reg, OCTEON_LMC_RLEVEL_RANK)) {
            value = octeon_lmc_get(lmc, reg) | OCTEON_LMC_RLEVEL_STATUS;
        } else if (octeon_lmc_rank_reg(reg, OCTEON_LMC_WLEVEL_RANK)) {
            value = octeon_lmc_get(lmc, reg) | OCTEON_LMC_WLEVEL_STATUS;
        } else {
            value = octeon_lmc_get(lmc, reg);
        }
        break;
    }

    return octeon_read64(value, addr, size);
}

static void octeon_lmc_write(OcteonLmcState *lmc, hwaddr reg,
                             hwaddr addr, uint64_t value, unsigned size)
{
    uint64_t old = octeon_lmc_get(lmc, reg);

    value = octeon_write64(old, addr, value, size);
    octeon_reg_store(lmc->regs, reg, value);
}

static bool octeon_ciu3_source_from_intsn(uint32_t intsn,
                                          unsigned int *source)
{
    if (intsn == OCTEON_CIU3_PEM_INTSN_INTA(0)) {
        *source = OCTEON_CIU3_SRC_PCI_INTA;
        return true;
    }
    if (intsn == OCTEON_CIU3_UART0_INTSN) {
        *source = OCTEON_CIU3_SRC_UART0;
        return true;
    }
    if (intsn == OCTEON_CIU3_USB0_INTSN) {
        *source = OCTEON_CIU3_SRC_USB0;
        return true;
    }
    if (intsn == OCTEON_CIU3_USB1_INTSN) {
        *source = OCTEON_CIU3_SRC_USB1;
        return true;
    }

    if (intsn >= OCTEON_CIU3_MBOX_INTSN(0) &&
        intsn < OCTEON_CIU3_MBOX_INTSN(OCTEON_MAX_CPUS)) {
        *source = OCTEON_CIU3_SRC_MBOX0 +
                  intsn - OCTEON_CIU3_MBOX_INTSN(0);
        return true;
    }

    return false;
}

static uint32_t octeon_ciu3_source_intsn(unsigned int source)
{
    switch (source) {
    case OCTEON_CIU3_SRC_PCI_INTA:
        return OCTEON_CIU3_PEM_INTSN_INTA(0);
    case OCTEON_CIU3_SRC_UART0:
        return OCTEON_CIU3_UART0_INTSN;
    case OCTEON_CIU3_SRC_USB0:
        return OCTEON_CIU3_USB0_INTSN;
    case OCTEON_CIU3_SRC_USB1:
        return OCTEON_CIU3_USB1_INTSN;
    }

    if (source >= OCTEON_CIU3_SRC_MBOX0 &&
        source < OCTEON_CIU3_SRC_COUNT) {
        return OCTEON_CIU3_MBOX_INTSN(source - OCTEON_CIU3_SRC_MBOX0);
    }

    g_assert_not_reached();
}

static bool octeon_ciu3_source_is_level(unsigned int source)
{
    switch (source) {
    case OCTEON_CIU3_SRC_PCI_INTA:
    case OCTEON_CIU3_SRC_UART0:
    case OCTEON_CIU3_SRC_USB0:
    case OCTEON_CIU3_SRC_USB1:
        return true;
    }

    return false;
}

static bool octeon_ciu3_set_level(OcteonState *s, unsigned int source,
                                  int level)
{
    uint32_t old;
    uint32_t new;
    uint32_t cmp = qatomic_read(&s->ciu3_src_state[source]);

    do {
        old = cmp;
        if (level) {
            new = old | OCTEON_CIU3_SRC_LEVEL | OCTEON_CIU3_SRC_RAW;
        } else {
            new = old & ~(OCTEON_CIU3_SRC_LEVEL | OCTEON_CIU3_SRC_RAW);
        }
        if (new == old) {
            return false;
        }
        cmp = qatomic_cmpxchg(&s->ciu3_src_state[source], old, new);
    } while (old != cmp);

    return true;
}

static void octeon_ciu3_clear_raw(OcteonState *s, unsigned int source)
{
    uint32_t old;
    uint32_t new;
    uint32_t cmp = qatomic_read(&s->ciu3_src_state[source]);

    do {
        old = cmp;
        if (octeon_ciu3_source_is_level(source) &&
            (old & OCTEON_CIU3_SRC_LEVEL)) {
            new = old | OCTEON_CIU3_SRC_RAW;
        } else {
            new = old & ~OCTEON_CIU3_SRC_RAW;
        }
        if (new == old) {
            return;
        }
        cmp = qatomic_cmpxchg(&s->ciu3_src_state[source], old, new);
    } while (old != cmp);
}

static bool octeon_ciu3_set_mbox(OcteonState *s, unsigned int cpu)
{
    return octeon_ciu3_set_level(s, OCTEON_CIU3_SRC_MBOX0 + cpu,
                                 qatomic_read(&s->ciu_mbox[cpu]) != 0);
}

static bool octeon_ciu3_isc_decode(hwaddr reg, uint32_t base,
                                   uint32_t *intsn)
{
    if (reg < base || reg >= base + OCTEON_CIU3_ISC_SIZE) {
        return false;
    }

    *intsn = (reg - base) >> 3;
    return true;
}

static uint64_t octeon_ciu3_isc_read(OcteonState *s, uint32_t intsn)
{
    unsigned int src;
    uint64_t value;

    if (!octeon_ciu3_source_from_intsn(intsn, &src)) {
        return 0;
    }

    value = qatomic_read(&s->ciu3_src_ctl[src]) | OCTEON_CIU3_ISC_CTL_IMP;
    if (qatomic_read(&s->ciu3_src_state[src]) & OCTEON_CIU3_SRC_RAW) {
        value |= OCTEON_CIU3_ISC_CTL_RAW;
    }
    return value;
}

static bool octeon_ciu3_source_pending(OcteonState *s, unsigned int cpu_index,
                                       unsigned int irq, unsigned int src)
{
    uint64_t ctl = qatomic_read(&s->ciu3_src_ctl[src]);
    unsigned int idt;
    unsigned int output;

    if (!(qatomic_read(&s->ciu3_src_state[src]) & OCTEON_CIU3_SRC_RAW) ||
        !(ctl & OCTEON_CIU3_ISC_CTL_EN)) {
        return false;
    }

    idt = (ctl & OCTEON_CIU3_ISC_CTL_IDT) >>
          OCTEON_CIU3_ISC_CTL_IDT_SHIFT;
    if (idt >= OCTEON_CIU3_IDT_COUNT) {
        return false;
    }

    output = qatomic_read(&s->ciu3_idt_ctl[idt]) &
             OCTEON_CIU3_IDT_CTL_IP_NUM;
    if (output >= OCTEON_CIU3_CP0_IRQ_COUNT ||
        irq != OCTEON_CIU3_CP0_IRQ_BASE + output) {
        return false;
    }

    return qatomic_read(&s->ciu3_idt_pp[idt]) & (1ULL << cpu_index);
}

static bool octeon_ciu3_pending_intsn(OcteonState *s, unsigned int cpu_index,
                                      unsigned int irq, uint32_t *intsn)
{
    unsigned int src;

    for (src = 0; src < OCTEON_CIU3_SRC_COUNT; src++) {
        if (octeon_ciu3_source_pending(s, cpu_index, irq, src)) {
            *intsn = octeon_ciu3_source_intsn(src);
            return true;
        }
    }

    return false;
}

static bool octeon_ciu3_pending_intsn_from(OcteonState *s,
                                           unsigned int cpu_index,
                                           unsigned int irq,
                                           unsigned int first_src,
                                           uint32_t *intsn,
                                           unsigned int *source)
{
    unsigned int i;
    unsigned int src;

    for (i = 0; i < OCTEON_CIU3_SRC_COUNT; i++) {
        src = (first_src + i) % OCTEON_CIU3_SRC_COUNT;
        if (octeon_ciu3_source_pending(s, cpu_index, irq, src)) {
            *intsn = octeon_ciu3_source_intsn(src);
            *source = src;
            return true;
        }
    }

    return false;
}

static bool octeon_ciu3_pending_dest_intsn(OcteonState *s,
                                           unsigned int cpu_index,
                                           uint32_t *intsn)
{
    unsigned int irq;
    unsigned int irq_index;
    unsigned int src;

    for (irq = OCTEON_CIU3_CP0_IRQ_BASE;
         irq < OCTEON_CIU3_CP0_IRQ_BASE + OCTEON_CIU3_CP0_IRQ_COUNT;
         irq++) {
        irq_index = irq - OCTEON_CIU3_CP0_IRQ_BASE;
        if (octeon_ciu3_pending_intsn_from(s, cpu_index, irq,
                qatomic_read(&s->ciu3_src_cursor[cpu_index][irq_index]),
                intsn, &src)) {
            qatomic_set(&s->ciu3_src_cursor[cpu_index][irq_index],
                        (src + 1) % OCTEON_CIU3_SRC_COUNT);
            return true;
        }
    }

    return false;
}

static void octeon_ciu3_set_cpu_irq(OcteonState *s, unsigned int cpu_index,
                                    unsigned int irq, bool level)
{
    CPUMIPSState *env = &s->cpu[cpu_index].cpu->env;
    unsigned int irq_index = irq - OCTEON_CIU3_CP0_IRQ_BASE;
    uint8_t new_level = level;
    uint8_t old_level;

    old_level = qatomic_xchg(&s->ciu3_irq_line[cpu_index][irq_index],
                             new_level);
    if (old_level == new_level) {
        return;
    }

    qemu_set_irq(env->irq[irq], level);
}

static void octeon_intc_update_cpu(OcteonState *s, unsigned int cpu_index)
{
    uint32_t intsn;
    unsigned int irq;
    bool level;

    if (cpu_index >= s->cpu_count || !s->cpu[cpu_index].cpu) {
        return;
    }

    if (qatomic_read(&s->ciu3_pp_rst) & (1ULL << cpu_index)) {
        for (irq = OCTEON_CIU3_CP0_IRQ_BASE;
             irq < OCTEON_CIU3_CP0_IRQ_BASE + OCTEON_CIU3_CP0_IRQ_COUNT;
             irq++) {
            octeon_ciu3_set_cpu_irq(s, cpu_index, irq, false);
        }
        return;
    }

    for (irq = OCTEON_CIU3_CP0_IRQ_BASE;
         irq < OCTEON_CIU3_CP0_IRQ_BASE + OCTEON_CIU3_CP0_IRQ_COUNT;
         irq++) {
        level = false;
        if (irq == 3) {
            level = qatomic_read(&s->ciu_mbox[cpu_index]) &&
                    (qatomic_read(&s->ciu_ipi_en[cpu_index]) &
                     OCTEON_CIU_SUM_IPI);
        }

        if (octeon_ciu3_pending_intsn(s, cpu_index, irq, &intsn)) {
            level = true;
        }

        octeon_ciu3_set_cpu_irq(s, cpu_index, irq, level);
    }
}

static void octeon_intc_request_cpu_update(OcteonState *s,
                                           unsigned int cpu_index)
{
    if (cpu_index >= s->cpu_count || !s->cpu[cpu_index].cpu) {
        return;
    }

    octeon_intc_update_cpu(s, cpu_index);
}

static void octeon_intc_update_cpu_mask(OcteonState *s, uint64_t cpu_mask)
{
    unsigned int cpu;

    for (cpu = 0; cpu < s->cpu_count; cpu++) {
        if (cpu_mask & (1ULL << cpu)) {
            octeon_intc_request_cpu_update(s, cpu);
        }
    }
}

static void octeon_intc_update_all_cpus(OcteonState *s)
{
    unsigned int cpu;

    for (cpu = 0; cpu < s->cpu_count; cpu++) {
        octeon_intc_request_cpu_update(s, cpu);
    }
}

static uint64_t octeon_present_cpu_mask(OcteonState *s)
{
    return (1ULL << s->cpu_count) - 1;
}

static uint64_t octeon_secondary_cpu_mask(OcteonState *s)
{
    return octeon_present_cpu_mask(s) & ~1ULL;
}

static uint64_t octeon_ciu3_ctl_cpu_mask(OcteonState *s, uint64_t ctl)
{
    unsigned int idt = (ctl & OCTEON_CIU3_ISC_CTL_IDT) >>
                       OCTEON_CIU3_ISC_CTL_IDT_SHIFT;

    if (idt >= OCTEON_CIU3_IDT_COUNT) {
        return 0;
    }

    return qatomic_read(&s->ciu3_idt_pp[idt]);
}

static uint64_t octeon_ciu3_source_cpu_mask(OcteonState *s, unsigned int src)
{
    return octeon_ciu3_ctl_cpu_mask(s, qatomic_read(&s->ciu3_src_ctl[src]));
}

static uint64_t octeon_ciu_gpio_rx_value(OcteonState *s)
{
    uint64_t value = 0;
    unsigned int i;

    for (i = 0; i < OCTEON_CIU_GPIO_COUNT; i++) {
        uint64_t mask = 1ULL << i;

        if (qatomic_read(&s->ciu_gpio_bit_cfg[i]) &
            OCTEON_CIU_GPIO_BIT_CFG_TX_OE) {
            if (qatomic_read(&s->ciu_gpio_tx) & mask) {
                value |= mask;
            }
        } else if (qatomic_read(&s->ciu_gpio_rx) & mask) {
            value |= mask;
        }
    }

    return value;
}

static uint64_t octeon_ciu_read(void *opaque, hwaddr addr, unsigned size)
{
    OcteonState *s = opaque;
    uint64_t value = 0;
    hwaddr reg = addr & ~7ULL;
    unsigned int cpu;

    if (reg == 0) {
        if (qatomic_read(&s->uart_pending)) {
            value |= OCTEON_CIU_SUM_UART;
        }
        if (qatomic_read(&s->pci_pending)) {
            value |= OCTEON_CIU_SUM_PCI;
        }
        octeon_intc_request_cpu_update(s, 0);
        return octeon_read64(value, addr, size);
    }

    if (reg == OCTEON_CIU_IPI_EN_BASE) {
        return octeon_read64(OCTEON_CIU_ENABLE0, addr, size);
    }

    if (reg == OCTEON_CIU_FUSE) {
        value = octeon_present_cpu_mask(s);
        return octeon_read64(value, addr, size);
    }

    if (reg == OCTEON_CIU_GPIO_RX_DAT) {
        return octeon_read64(octeon_ciu_gpio_rx_value(s), addr, size);
    }

    if (reg == OCTEON_CIU_GPIO_TX_SET ||
        reg == OCTEON_CIU_GPIO_TX_CLR) {
        return octeon_read64(qatomic_read(&s->ciu_gpio_tx), addr, size);
    }

    if (reg >= OCTEON_CIU_GPIO_BIT_CFGX(0) &&
        reg < OCTEON_CIU_GPIO_BIT_CFGX(OCTEON_CIU_GPIO_COUNT)) {
        unsigned int index = (reg - OCTEON_CIU_GPIO_BIT_CFGX(0)) /
                             OCTEON_CIU_GPIO_BIT_CFG_STRIDE;

        return octeon_read64(qatomic_read(&s->ciu_gpio_bit_cfg[index]),
                             addr, size);
    }

    if (reg >= OCTEON_CIU_IPI_SUM_BASE &&
        reg < OCTEON_CIU_IPI_SUM_BASE +
              OCTEON_MAX_CPUS * OCTEON_CIU_IPI_SUM_STRIDE &&
        ((reg - OCTEON_CIU_IPI_SUM_BASE) &
         (OCTEON_CIU_IPI_SUM_STRIDE - 1)) == 0) {
        cpu = (reg - OCTEON_CIU_IPI_SUM_BASE) / OCTEON_CIU_IPI_SUM_STRIDE;
        if (cpu < s->cpu_count && qatomic_read(&s->ciu_mbox[cpu])) {
            return octeon_read64(OCTEON_CIU_SUM_IPI, addr, size);
        }
    }

    if (reg >= OCTEON_CIU_IPI_EN_BASE &&
        reg < OCTEON_CIU_IPI_EN_BASE +
              OCTEON_MAX_CPUS * OCTEON_CIU_IPI_EN_STRIDE &&
        ((reg - OCTEON_CIU_IPI_EN_BASE) &
         (OCTEON_CIU_IPI_EN_STRIDE - 1)) == 0) {
        cpu = (reg - OCTEON_CIU_IPI_EN_BASE) / OCTEON_CIU_IPI_EN_STRIDE;
        if (cpu < s->cpu_count) {
            return octeon_read64(qatomic_read(&s->ciu_ipi_en[cpu]),
                                 addr, size);
        }
    }

    if (reg >= OCTEON_CIU_MBOX_CLR_BASE &&
        reg < OCTEON_CIU_MBOX_CLR_BASE +
              OCTEON_MAX_CPUS * OCTEON_CIU_MBOX_SET_STRIDE &&
        ((reg - OCTEON_CIU_MBOX_CLR_BASE) &
         (OCTEON_CIU_MBOX_SET_STRIDE - 1)) == 0) {
        cpu = (reg - OCTEON_CIU_MBOX_CLR_BASE) / OCTEON_CIU_MBOX_SET_STRIDE;
        if (cpu < s->cpu_count) {
            return octeon_read64(qatomic_read(&s->ciu_mbox[cpu]), addr, size);
        }
    }

    return 0;
}

static void octeon_ciu_write(void *opaque, hwaddr addr,
                             uint64_t value, unsigned size)
{
    OcteonState *s = opaque;
    hwaddr reg = addr & ~7ULL;
    uint64_t old;
    unsigned int cpu;

    if (reg >= OCTEON_CIU_IPI_EN_BASE &&
        reg < OCTEON_CIU_IPI_EN_BASE +
              OCTEON_MAX_CPUS * OCTEON_CIU_IPI_EN_STRIDE &&
        ((reg - OCTEON_CIU_IPI_EN_BASE) &
         (OCTEON_CIU_IPI_EN_STRIDE - 1)) == 0) {
        cpu = (reg - OCTEON_CIU_IPI_EN_BASE) / OCTEON_CIU_IPI_EN_STRIDE;
        if (cpu < s->cpu_count) {
            value = octeon_atomic_write64(&s->ciu_ipi_en[cpu], addr, value,
                                          size, UINT64_MAX, 0, &old);
            if (old != value) {
                octeon_intc_request_cpu_update(s, cpu);
            }
        }
        return;
    }

    if (reg >= OCTEON_CIU_MBOX_SET_BASE &&
        reg < OCTEON_CIU_MBOX_SET_BASE +
              OCTEON_MAX_CPUS * OCTEON_CIU_MBOX_SET_STRIDE &&
        ((reg - OCTEON_CIU_MBOX_SET_BASE) &
         (OCTEON_CIU_MBOX_SET_STRIDE - 1)) == 0) {
        cpu = (reg - OCTEON_CIU_MBOX_SET_BASE) / OCTEON_CIU_MBOX_SET_STRIDE;
        if (cpu < s->cpu_count) {
            old = qatomic_fetch_or(&s->ciu_mbox[cpu], value & UINT32_MAX);
            if ((old | (value & UINT32_MAX)) != old &&
                octeon_ciu3_set_mbox(s, cpu)) {
                octeon_intc_request_cpu_update(s, cpu);
            }
        }
        return;
    }

    if (reg >= OCTEON_CIU_MBOX_CLR_BASE &&
        reg < OCTEON_CIU_MBOX_CLR_BASE +
              OCTEON_MAX_CPUS * OCTEON_CIU_MBOX_SET_STRIDE &&
        ((reg - OCTEON_CIU_MBOX_CLR_BASE) &
         (OCTEON_CIU_MBOX_SET_STRIDE - 1)) == 0) {
        cpu = (reg - OCTEON_CIU_MBOX_CLR_BASE) / OCTEON_CIU_MBOX_SET_STRIDE;
        if (cpu < s->cpu_count) {
            old = qatomic_fetch_and(&s->ciu_mbox[cpu],
                                    ~(value & UINT32_MAX));
            if ((old & (value & UINT32_MAX)) &&
                octeon_ciu3_set_mbox(s, cpu)) {
                octeon_intc_request_cpu_update(s, cpu);
            }
        }
        return;
    }

    if (reg == OCTEON_CIU_GPIO_TX_SET) {
        value = octeon_write64(0, addr, value, size) &
                OCTEON_CIU_GPIO_INPUTS;
        qatomic_or(&s->ciu_gpio_tx, value);
        return;
    }

    if (reg == OCTEON_CIU_GPIO_TX_CLR) {
        value = octeon_write64(0, addr, value, size) &
                OCTEON_CIU_GPIO_INPUTS;
        qatomic_and(&s->ciu_gpio_tx, ~value);
        return;
    }

    if (reg >= OCTEON_CIU_GPIO_BIT_CFGX(0) &&
        reg < OCTEON_CIU_GPIO_BIT_CFGX(OCTEON_CIU_GPIO_COUNT)) {
        unsigned int index = (reg - OCTEON_CIU_GPIO_BIT_CFGX(0)) /
                             OCTEON_CIU_GPIO_BIT_CFG_STRIDE;

        octeon_atomic_write64(&s->ciu_gpio_bit_cfg[index], addr, value,
                              size, UINT64_MAX, 0, NULL);
        return;
    }
}

static void octeon_cpu_park_now(MIPSCPU *cpu)
{
    CPUMIPSState *env = &cpu->env;
    CPUState *cs = CPU(cpu);

    env->active_tc.CP0_TCHalt = 1;
    env->tcs[0].CP0_TCHalt = 1;
    cs->halted = 1;
    cpu_reset_interrupt(cs, CPU_INTERRUPT_WAKE);
}

static void octeon_cpu_park_work(CPUState *cs, run_on_cpu_data data)
{
    octeon_cpu_park_now(MIPS_CPU(cs));
}

static void octeon_cpu_start_work(CPUState *cs, run_on_cpu_data data)
{
    MIPSCPU *cpu = MIPS_CPU(cs);
    CPUMIPSState *env = &cpu->env;

    cpu->mvp->CP0_MVPControl |= (1 << CP0MVPCo_EVP);
    env->CP0_VPEConf0 |= (1 << CP0VPEC0_MVP) | (1 << CP0VPEC0_VPA);
    env->active_tc.CP0_TCHalt = 0;
    env->tcs[0].CP0_TCHalt = 0;
    env->active_tc.CP0_TCStatus = (1 << CP0TCSt_A);
    env->tcs[0].CP0_TCStatus = (1 << CP0TCSt_A);
    env->active_tc.PC = cpu_mips_phys_to_kseg1(NULL, OCTEON_FLASH_RESET_BASE);
    cs->halted = 0;
    cpu_interrupt(cs, CPU_INTERRUPT_WAKE | CPU_INTERRUPT_EXITTB);
}

static void octeon_start_cpu(OcteonState *s, unsigned int cpu_index)
{
    MIPSCPU *cpu;

    if (cpu_index >= s->cpu_count || !s->cpu[cpu_index].cpu) {
        return;
    }

    cpu = s->cpu[cpu_index].cpu;
    async_run_on_cpu(CPU(cpu), octeon_cpu_start_work, RUN_ON_CPU_NULL);
}

static void octeon_ciu3_start_cpu(OcteonState *s, unsigned int cpu_index)
{
    if (qatomic_read(&s->ciu3_pp_rst) & (1ULL << cpu_index)) {
        return;
    }

    octeon_start_cpu(s, cpu_index);
}

static void octeon_ciu3_nmi(OcteonState *s, uint64_t mask)
{
    unsigned int cpu;

    for (cpu = 1; cpu < s->cpu_count; cpu++) {
        if (mask & (1ULL << cpu)) {
            octeon_ciu3_start_cpu(s, cpu);
        }
    }
}

static void octeon_rst_pp_power_write(OcteonState *s, hwaddr addr,
                                      uint64_t value, unsigned size)
{
    uint64_t old = s->rst_pp_power;
    uint64_t changed;
    unsigned int cpu;

    value = octeon_write64(old, addr, value, size) &
            octeon_present_cpu_mask(s);
    s->rst_pp_power = value;
    changed = old ^ value;

    for (cpu = 1; cpu < s->cpu_count; cpu++) {
        uint64_t mask = 1ULL << cpu;

        if (!(changed & mask)) {
            continue;
        }
        if (value & mask) {
            async_run_on_cpu(CPU(s->cpu[cpu].cpu), octeon_cpu_park_work,
                             RUN_ON_CPU_NULL);
        } else {
            octeon_start_cpu(s, cpu);
        }
    }
}

static const MemoryRegionOps octeon_ciu_ops = {
    .read = octeon_ciu_read,
    .write = octeon_ciu_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
    .valid = {
        .min_access_size = 4,
        .max_access_size = 8,
    },
};

static uint64_t octeon_ciu3_read(void *opaque, hwaddr addr, unsigned size)
{
    OcteonState *s = opaque;
    hwaddr reg = addr & ~7ULL;
    uint64_t value;
    uint32_t intsn;
    unsigned int cpu;
    unsigned int idt;

    if (reg == OCTEON_CIU3_PP_RST) {
        return octeon_read64(qatomic_read(&s->ciu3_pp_rst), addr, size);
    }

    if (reg == OCTEON_CIU3_PP_RST_PENDING) {
        return 0;
    }

    if (reg == OCTEON_CIU3_NMI) {
        return 0;
    }

    if (reg == OCTEON_CIU3_FUSE) {
        value = octeon_present_cpu_mask(s);
        return octeon_read64(value, addr, size);
    }

    if (reg >= OCTEON_CIU3_IDT_CTL(0) &&
        reg < OCTEON_CIU3_IDT_CTL(OCTEON_CIU3_IDT_COUNT)) {
        idt = (reg - OCTEON_CIU3_IDT_CTL(0)) >> 3;
        return octeon_read64(qatomic_read(&s->ciu3_idt_ctl[idt]), addr, size);
    }

    if (reg >= OCTEON_CIU3_IDT_PP(0) &&
        reg < OCTEON_CIU3_IDT_PP(OCTEON_CIU3_IDT_COUNT)) {
        idt = (reg - OCTEON_CIU3_IDT_PP(0)) >> 5;
        return octeon_read64(qatomic_read(&s->ciu3_idt_pp[idt]), addr, size);
    }

    if (reg >= OCTEON_CIU3_IDT_IO(0) &&
        reg < OCTEON_CIU3_IDT_IO(OCTEON_CIU3_IDT_COUNT)) {
        idt = (reg - OCTEON_CIU3_IDT_IO(0)) >> 3;
        return octeon_read64(qatomic_read(&s->ciu3_idt_io[idt]), addr, size);
    }

    if (reg >= OCTEON_CIU3_DEST_PP_INT(0) &&
        reg < OCTEON_CIU3_DEST_PP_INT(OCTEON_MAX_CPUS)) {
        cpu = (reg - OCTEON_CIU3_DEST_PP_INT(0)) >> 3;
        value = 0;
        if (cpu < s->cpu_count) {
            if (!(qatomic_read(&s->ciu3_pp_rst) & (1ULL << cpu)) &&
                octeon_ciu3_pending_dest_intsn(s, cpu, &intsn)) {
                value = OCTEON_CIU3_DEST_PP_INT_INTR |
                        ((uint64_t)intsn <<
                         OCTEON_CIU3_DEST_PP_INT_INTSN_SHIFT);
            }
        }
        return octeon_read64(value, addr, size);
    }

    if (octeon_ciu3_isc_decode(reg, OCTEON_CIU3_ISC_CTL_BASE, &intsn)) {
        return octeon_read64(octeon_ciu3_isc_read(s, intsn), addr, size);
    }

    if (octeon_ciu3_isc_decode(reg, OCTEON_CIU3_ISC_W1C_BASE, &intsn) ||
        octeon_ciu3_isc_decode(reg, OCTEON_CIU3_ISC_W1S_BASE, &intsn)) {
        return octeon_read64(octeon_ciu3_isc_read(s, intsn), addr, size);
    }

    return 0;
}

static void octeon_ciu3_write(void *opaque, hwaddr addr,
                              uint64_t value, unsigned size)
{
    OcteonState *s = opaque;
    hwaddr reg = addr & ~7ULL;
    uint64_t old;
    uint32_t intsn;
    unsigned int cpu;
    unsigned int idt;
    unsigned int src;

    if (reg == OCTEON_CIU3_PP_RST) {
        value = octeon_atomic_write64(&s->ciu3_pp_rst, addr, value, size,
                                      octeon_present_cpu_mask(s), 0, &old);

        for (cpu = 1; cpu < s->cpu_count; cpu++) {
            uint64_t mask = 1ULL << cpu;

            if (!((old ^ value) & mask)) {
                continue;
            }
            if (value & mask) {
                async_run_on_cpu(CPU(s->cpu[cpu].cpu), octeon_cpu_park_work,
                                 RUN_ON_CPU_NULL);
            } else {
                octeon_ciu3_start_cpu(s, cpu);
            }
        }
        octeon_intc_update_all_cpus(s);
        return;
    }

    if (reg == OCTEON_CIU3_NMI) {
        octeon_ciu3_nmi(s, octeon_write64(0, addr, value, size));
        octeon_intc_update_all_cpus(s);
        return;
    }

    if (reg >= OCTEON_CIU3_IDT_CTL(0) &&
        reg < OCTEON_CIU3_IDT_CTL(OCTEON_CIU3_IDT_COUNT)) {
        idt = (reg - OCTEON_CIU3_IDT_CTL(0)) >> 3;
        octeon_atomic_write64(&s->ciu3_idt_ctl[idt], addr, value, size,
                              UINT64_MAX, 0, NULL);
        octeon_intc_update_cpu_mask(s, qatomic_read(&s->ciu3_idt_pp[idt]));
        return;
    }

    if (reg >= OCTEON_CIU3_IDT_PP(0) &&
        reg < OCTEON_CIU3_IDT_PP(OCTEON_CIU3_IDT_COUNT)) {
        idt = (reg - OCTEON_CIU3_IDT_PP(0)) >> 5;
        value = octeon_atomic_write64(&s->ciu3_idt_pp[idt], addr, value,
                                      size, UINT64_MAX, 0, &old);
        octeon_intc_update_cpu_mask(s, old | value);
        return;
    }

    if (reg >= OCTEON_CIU3_IDT_IO(0) &&
        reg < OCTEON_CIU3_IDT_IO(OCTEON_CIU3_IDT_COUNT)) {
        idt = (reg - OCTEON_CIU3_IDT_IO(0)) >> 3;
        octeon_atomic_write64(&s->ciu3_idt_io[idt], addr, value, size,
                              UINT64_MAX, 0, NULL);
        octeon_intc_update_cpu_mask(s, qatomic_read(&s->ciu3_idt_pp[idt]));
        return;
    }

    if (octeon_ciu3_isc_decode(reg, OCTEON_CIU3_ISC_CTL_BASE, &intsn)) {
        if (octeon_ciu3_source_from_intsn(intsn, &src)) {
            value = octeon_atomic_write64(&s->ciu3_src_ctl[src], addr, value,
                                          size, ~OCTEON_CIU3_ISC_CTL_RAW,
                                          0, &old);
            octeon_intc_update_cpu_mask(s,
                                        octeon_ciu3_ctl_cpu_mask(s, old) |
                                        octeon_ciu3_ctl_cpu_mask(s, value));
        }
        return;
    }

    if (octeon_ciu3_isc_decode(reg, OCTEON_CIU3_ISC_W1C_BASE, &intsn)) {
        if (octeon_ciu3_source_from_intsn(intsn, &src)) {
            old = qatomic_read(&s->ciu3_src_ctl[src]);
            if (value & OCTEON_CIU3_ISC_CTL_EN) {
                qatomic_and(&s->ciu3_src_ctl[src], ~OCTEON_CIU3_ISC_CTL_EN);
            }
            if (value & OCTEON_CIU3_ISC_CTL_RAW) {
                octeon_ciu3_clear_raw(s, src);
            }
            octeon_intc_update_cpu_mask(s,
                                        octeon_ciu3_ctl_cpu_mask(s, old) |
                                        octeon_ciu3_source_cpu_mask(s, src));
        }
        return;
    }

    if (octeon_ciu3_isc_decode(reg, OCTEON_CIU3_ISC_W1S_BASE, &intsn)) {
        if (octeon_ciu3_source_from_intsn(intsn, &src)) {
            old = qatomic_read(&s->ciu3_src_ctl[src]);
            if (value & OCTEON_CIU3_ISC_CTL_EN) {
                qatomic_or(&s->ciu3_src_ctl[src], OCTEON_CIU3_ISC_CTL_EN);
            }
            if (value & OCTEON_CIU3_ISC_CTL_RAW) {
                qatomic_or(&s->ciu3_src_state[src], OCTEON_CIU3_SRC_RAW);
            }
            octeon_intc_update_cpu_mask(s,
                                        octeon_ciu3_ctl_cpu_mask(s, old) |
                                        octeon_ciu3_source_cpu_mask(s, src));
        }
    }
}

static const MemoryRegionOps octeon_ciu3_ops = {
    .read = octeon_ciu3_read,
    .write = octeon_ciu3_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
    .valid = {
        .min_access_size = 4,
        .max_access_size = 8,
    },
};

static uint64_t octeon_csr_read(void *opaque, hwaddr addr, unsigned size)
{
    OcteonState *s = opaque;
    OcteonLmcState *lmc;
    hwaddr reg = addr & ~7ULL;
    hwaddr lreg;
    hwaddr ereg;
    uint64_t value;

    if (reg >= OCTEON_MIO_BOOT_REG_CFGX(0) &&
        reg < OCTEON_MIO_BOOT_REG_CFGX(OCTEON_MIO_BOOT_REG_CFG_COUNT)) {
        unsigned int index = OCTEON_MIO_BOOT_REG_CFG_INDEX(reg);

        return octeon_read64(s->mio_boot_reg_cfg[index], addr, size);
    }

    switch (reg) {
    case OCTEON_MIO_BOOT_LOC_CFGX(0):
    case OCTEON_MIO_BOOT_LOC_CFGX(1): {
        unsigned int index = OCTEON_MIO_BOOT_LOC_CFG_INDEX(reg);

        return octeon_read64(s->mio_boot_loc_cfg[index], addr, size);
    }
    case OCTEON_MIO_BOOT_LOC_ADR:
        return octeon_read64(s->mio_boot_loc_adr, addr, size);
    case OCTEON_MIO_BOOT_LOC_DAT:
        return octeon_read64(octeon_mio_boot_loc_dat_read(s), addr, size);
    case OCTEON_MIO_FUS_DAT2:
        return 0;
    case OCTEON_MIO_FUS_RCMD:
        return octeon_read64(s->mio_fus_rcmd, addr, size);
    case OCTEON_RST_BOOT:
        value = ((uint64_t)(s->cpu_hz / s->ref_hz) <<
                 OCTEON_RST_BOOT_C_MUL_SHIFT) |
                ((uint64_t)(s->io_hz / s->ref_hz) <<
                 OCTEON_RST_BOOT_PNR_MUL_SHIFT);
        return octeon_read64(value, addr, size);
    case OCTEON_RST_PP_POWER:
        return octeon_read64(s->rst_pp_power, addr, size);
    case OCTEON_GSERX_CFG0:
        return octeon_read64(1, addr, size);
    case OCTEON_GSERX_QLM_STAT0:
        return octeon_read64(3, addr, size);
    case OCTEON_RST_CTLX0:
        if (!octeon_csr_lookup(s, reg, &value)) {
            value = OCTEON_RST_CTL_HOST_MODE | OCTEON_RST_CTL_RST_DONE;
        }
        return octeon_read64(value, addr, size);
    case OCTEON_RST_SOFT_PRSTX0:
        if (!octeon_csr_lookup(s, reg, &value)) {
            value = 1;
        }
        return octeon_read64(value, addr, size);
    case OCTEON_PEMX_CFG_RD0:
        value = ((uint64_t)octeon_pcie_root_cfg_read(s,
                                                     s->pcie_cfg_rd_addr) <<
                 32) | s->pcie_cfg_rd_addr;
        return octeon_read64(value, addr, size);
    case OCTEON_PEMX_BIST_STATUS0:
        return 0;
    case OCTEON_PEMX_ON0:
        return octeon_read64(3, addr, size);
    case OCTEON_PEMX_STRAP0:
        return octeon_read64(3, addr, size);
    default:
        break;
    }

    if (octeon_lmc_decode(reg, &lmc, &lreg, s)) {
        return octeon_lmc_read(s, lmc, lreg, addr, size);
    }

    if (octeon_mio_emm_decode(reg, &ereg)) {
        return octeon_mio_emm_read(s, ereg, addr, size);
    }

    if (!octeon_csr_lookup(s, reg, &value)) {
        value = 0;
    }
    return octeon_read64(value, addr, size);
}

static void octeon_csr_write(void *opaque, hwaddr addr,
                             uint64_t value, unsigned size)
{
    OcteonState *s = opaque;
    OcteonLmcState *lmc;
    hwaddr reg = addr & ~7ULL;
    hwaddr lreg;
    hwaddr ereg;
    uint64_t old;

    if (octeon_lmc_decode(reg, &lmc, &lreg, s)) {
        octeon_lmc_write(lmc, lreg, addr, value, size);
        return;
    }

    if (octeon_mio_emm_decode(reg, &ereg)) {
        octeon_mio_emm_write(s, ereg, addr, value, size);
        return;
    }

    if (reg == OCTEON_RST_SOFT_RST && value) {
        qemu_system_reset_request(SHUTDOWN_CAUSE_GUEST_RESET);
        return;
    }

    if (reg == OCTEON_MIO_BOOT_LOC_CFGX(0) ||
        reg == OCTEON_MIO_BOOT_LOC_CFGX(1)) {
        unsigned int index = OCTEON_MIO_BOOT_LOC_CFG_INDEX(reg);

        old = s->mio_boot_loc_cfg[index];
        s->mio_boot_loc_cfg[index] = octeon_write64(old, addr, value, size);
        if (index == 0) {
            octeon_mio_boot_loc_update(s);
        }
        return;
    }

    if (reg == OCTEON_MIO_BOOT_LOC_ADR) {
        old = s->mio_boot_loc_adr;
        value = octeon_write64(old, addr, value, size);
        s->mio_boot_loc_adr = value & OCTEON_MIO_BOOT_LOC_ADR_MASK;
        return;
    }

    if (reg == OCTEON_MIO_BOOT_LOC_DAT) {
        octeon_mio_boot_loc_dat_write(s, addr, value, size);
        return;
    }

    if (reg == OCTEON_MIO_FUS_RCMD) {
        value = octeon_write64(s->mio_fus_rcmd, addr, value, size);
        s->mio_fus_rcmd = value & ~(OCTEON_MIO_FUS_RCMD_PEND |
                                    OCTEON_MIO_FUS_RCMD_DAT);
        return;
    }

    if (reg == OCTEON_RST_PP_POWER) {
        octeon_rst_pp_power_write(s, addr, value, size);
        return;
    }

    if (reg == OCTEON_PEMX_CFG_RD0) {
        if (!octeon_csr_lookup(s, reg, &old)) {
            old = 0;
        }
        value = octeon_write64(old, addr, value, size);
        s->pcie_cfg_rd_addr = value;
        return;
    }

    if (reg == OCTEON_PEMX_CFG_WR0) {
        if (!octeon_csr_lookup(s, reg, &old)) {
            old = 0;
        }
        value = octeon_write64(old, addr, value, size);
        octeon_pcie_root_cfg_write(s, value, value >> 32);
        octeon_csr_store(s, reg, value);
        return;
    }

    if (reg >= OCTEON_MIO_BOOT_REG_CFGX(0) &&
        reg < OCTEON_MIO_BOOT_REG_CFGX(OCTEON_MIO_BOOT_REG_CFG_COUNT)) {
        unsigned int index = OCTEON_MIO_BOOT_REG_CFG_INDEX(reg);

        old = s->mio_boot_reg_cfg[index];
        s->mio_boot_reg_cfg[index] = octeon_write64(old, addr, value, size);
        if (index == OCTEON_BOOTBUS_LED_CS) {
            octeon_bootbus_led_update(s);
        }
        return;
    }

    if (!octeon_csr_lookup(s, reg, &old)) {
        old = 0;
    }
    value = octeon_write64(old, addr, value, size);
    octeon_csr_store(s, reg, value);
}

static const MemoryRegionOps octeon_csr_ops = {
    .read = octeon_csr_read,
    .write = octeon_csr_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
    .valid = {
        .min_access_size = 4,
        .max_access_size = 8,
    },
};

static uint64_t octeon_stateful_io_read(OcteonState *s, uint64_t base,
                                        hwaddr addr, unsigned size)
{
    uint64_t value;
    uint64_t reg = base + (addr & ~7ULL);

    if (!octeon_csr_lookup(s, reg, &value)) {
        value = 0;
    }

    return octeon_read64(value, addr, size);
}

static void octeon_stateful_io_write(OcteonState *s, uint64_t base,
                                     hwaddr addr, uint64_t value,
                                     unsigned size)
{
    uint64_t reg = base + (addr & ~7ULL);
    uint64_t old;

    if (!octeon_csr_lookup(s, reg, &old)) {
        old = 0;
    }

    octeon_csr_store(s, reg, octeon_write64(old, addr, value, size));
}

static uint64_t octeon_pexp_read(void *opaque, hwaddr addr, unsigned size)
{
    return octeon_stateful_io_read(opaque, OCTEON_PEXP_BASE, addr, size);
}

static void octeon_pexp_write(void *opaque, hwaddr addr,
                              uint64_t value, unsigned size)
{
    octeon_stateful_io_write(opaque, OCTEON_PEXP_BASE, addr, value, size);
}

static const MemoryRegionOps octeon_pexp_ops = {
    .read = octeon_pexp_read,
    .write = octeon_pexp_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
    .valid = {
        .min_access_size = 4,
        .max_access_size = 8,
    },
};

static uint64_t octeon_dpi_read(void *opaque, hwaddr addr, unsigned size)
{
    return octeon_stateful_io_read(opaque, OCTEON_DPI_BASE, addr, size);
}

static void octeon_dpi_write(void *opaque, hwaddr addr,
                             uint64_t value, unsigned size)
{
    octeon_stateful_io_write(opaque, OCTEON_DPI_BASE, addr, value, size);
}

static const MemoryRegionOps octeon_dpi_ops = {
    .read = octeon_dpi_read,
    .write = octeon_dpi_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
    .valid = {
        .min_access_size = 4,
        .max_access_size = 8,
    },
};

static uint64_t octeon_fpa_read(void *opaque, hwaddr addr, unsigned size)
{
    OcteonState *s = opaque;
    hwaddr reg = addr & ~7ULL;

    if (reg == OCTEON_FPA_CLK_COUNT) {
        return octeon_read64(octeon_clock_count(s->io_hz), addr, size);
    }

    return 0;
}

static void octeon_fpa_write(void *opaque, hwaddr addr,
                             uint64_t value, unsigned size)
{
}

static const MemoryRegionOps octeon_fpa_ops = {
    .read = octeon_fpa_read,
    .write = octeon_fpa_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
    .valid = {
        .min_access_size = 4,
        .max_access_size = 8,
    },
};

static uint64_t octeon_ipd_read(void *opaque, hwaddr addr, unsigned size)
{
    OcteonState *s = opaque;
    hwaddr reg = addr & ~7ULL;

    if (reg == OCTEON_IPD_CLK_COUNT) {
        return octeon_read64(octeon_clock_count(s->io_hz), addr, size);
    }

    return 0;
}

static void octeon_ipd_write(void *opaque, hwaddr addr,
                             uint64_t value, unsigned size)
{
}

static const MemoryRegionOps octeon_ipd_ops = {
    .read = octeon_ipd_read,
    .write = octeon_ipd_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
    .valid = {
        .min_access_size = 4,
        .max_access_size = 8,
    },
};

static uint64_t octeon_pko_read(void *opaque, hwaddr addr, unsigned size)
{
    return octeon_stateful_io_read(opaque, OCTEON_PKO_BASE, addr, size);
}

static void octeon_pko_write(void *opaque, hwaddr addr,
                             uint64_t value, unsigned size)
{
    octeon_stateful_io_write(opaque, OCTEON_PKO_BASE, addr, value, size);
}

static const MemoryRegionOps octeon_pko_ops = {
    .read = octeon_pko_read,
    .write = octeon_pko_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
    .valid = {
        .min_access_size = 4,
        .max_access_size = 8,
    },
};

static uint64_t octeon_rng_read(void *opaque, hwaddr addr, unsigned size)
{
    uint64_t value;

    qemu_guest_getrandom_nofail(&value, sizeof(value));
    return octeon_read64(value, addr, size);
}

static void octeon_rng_write(void *opaque, hwaddr addr,
                             uint64_t value, unsigned size)
{
}

static const MemoryRegionOps octeon_rng_ops = {
    .read = octeon_rng_read,
    .write = octeon_rng_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
    .valid = {
        .min_access_size = 4,
        .max_access_size = 8,
    },
};

static uint64_t octeon_pow_read(void *opaque, hwaddr addr, unsigned size)
{
    return octeon_stateful_io_read(opaque, OCTEON_POW_BASE, addr, size);
}

static void octeon_pow_write(void *opaque, hwaddr addr,
                             uint64_t value, unsigned size)
{
    octeon_stateful_io_write(opaque, OCTEON_POW_BASE, addr, value, size);
}

static const MemoryRegionOps octeon_pow_ops = {
    .read = octeon_pow_read,
    .write = octeon_pow_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
    .valid = {
        .min_access_size = 4,
        .max_access_size = 8,
    },
};

static uint64_t octeon_uctl_read(void *opaque, hwaddr addr, unsigned size)
{
    OcteonUsbState *usb = opaque;
    hwaddr reg = addr & ~7ULL;
    uint64_t value;

    if (!octeon_reg_lookup(usb->regs, reg, &value)) {
        value = 0;
    }

    return octeon_read64(value, addr, size);
}

static void octeon_uctl_write(void *opaque, hwaddr addr,
                              uint64_t value, unsigned size)
{
    OcteonUsbState *usb = opaque;
    hwaddr reg = addr & ~7ULL;
    uint64_t old;

    if (!octeon_reg_lookup(usb->regs, reg, &old)) {
        old = 0;
    }

    octeon_reg_store(usb->regs, reg, octeon_write64(old, addr, value, size));
}

static const MemoryRegionOps octeon_uctl_ops = {
    .read = octeon_uctl_read,
    .write = octeon_uctl_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
    .valid = {
        .min_access_size = 4,
        .max_access_size = 8,
    },
};

static bool octeon_usb_csr_needs_swap(OcteonUsbState *usb)
{
#if TARGET_BIG_ENDIAN
    uint64_t value;

    if (!octeon_reg_lookup(usb->regs, OCTEON_UCTL_SHIM_CFG, &value)) {
        return true;
    }

    return (value & OCTEON_UCTL_SHIM_CFG_CSR_BYTE_SWAP) !=
           OCTEON_UCTL_SHIM_CFG_CSR_NATIVE;
#else
    return false;
#endif
}

static uint64_t octeon_usb_swap(uint64_t value, unsigned size)
{
    switch (size) {
    case 2:
        return bswap16(value);
    case 4:
        return bswap32(value);
    case 8:
        return bswap64(value);
    default:
        return value;
    }
}

static uint64_t octeon_dwc3_window_read(void *opaque, hwaddr addr,
                                        unsigned size)
{
    OcteonUsbState *usb = opaque;
    uint64_t value;

    switch (size) {
    case 1:
        value = address_space_ldub(&usb->dwc3_as, addr,
                                   MEMTXATTRS_UNSPECIFIED, NULL);
        break;
    case 2:
        value = address_space_lduw_le(&usb->dwc3_as, addr,
                                      MEMTXATTRS_UNSPECIFIED, NULL);
        break;
    case 4:
        value = address_space_ldl_le(&usb->dwc3_as, addr,
                                     MEMTXATTRS_UNSPECIFIED, NULL);
        break;
    case 8:
        value = address_space_ldq_le(&usb->dwc3_as, addr,
                                     MEMTXATTRS_UNSPECIFIED, NULL);
        break;
    default:
        g_assert_not_reached();
    }

    if (octeon_usb_csr_needs_swap(usb)) {
        value = octeon_usb_swap(value, size);
    }

    return value;
}

static void octeon_dwc3_window_write(void *opaque, hwaddr addr,
                                     uint64_t value, unsigned size)
{
    OcteonUsbState *usb = opaque;

    if (octeon_usb_csr_needs_swap(usb)) {
        value = octeon_usb_swap(value, size);
    }

    switch (size) {
    case 1:
        address_space_stb(&usb->dwc3_as, addr, value,
                          MEMTXATTRS_UNSPECIFIED, NULL);
        break;
    case 2:
        address_space_stw_le(&usb->dwc3_as, addr, value,
                             MEMTXATTRS_UNSPECIFIED, NULL);
        break;
    case 4:
        address_space_stl_le(&usb->dwc3_as, addr, value,
                             MEMTXATTRS_UNSPECIFIED, NULL);
        break;
    case 8:
        address_space_stq_le(&usb->dwc3_as, addr, value,
                             MEMTXATTRS_UNSPECIFIED, NULL);
        break;
    default:
        g_assert_not_reached();
    }
}

static const MemoryRegionOps octeon_dwc3_window_ops = {
    .read = octeon_dwc3_window_read,
    .write = octeon_dwc3_window_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
    .valid = {
        .min_access_size = 1,
        .max_access_size = 8,
    },
};

static OcteonSpdEepromState *octeon_twsi_find_spd(OcteonTWSIState *t)
{
    OcteonState *s = t->board;
    unsigned int i;

    for (i = 0; i < OCTEON_SPD_EEPROM_COUNT; i++) {
        if (s->spd[i].bus == t->bus && s->spd[i].addr == t->slave_addr) {
            return &s->spd[i];
        }
    }

    return NULL;
}

static uint8_t octeon_spd_eeprom_read(OcteonSpdEepromState *eeprom)
{
    uint8_t value = 0xff;

    if (eeprom->offset < sizeof(eeprom->data)) {
        value = eeprom->data[eeprom->offset];
    }
    eeprom->offset++;
    return value;
}

static void octeon_twsi_write_ctl(OcteonTWSIState *t, uint8_t value)
{
    OcteonSpdEepromState *eeprom;

    t->ctl = value | OCTEON_TWSI_CTL_IFLG;

    if (value & OCTEON_TWSI_CTL_STA) {
        t->have_slave = false;
        t->addr_phase = false;
        t->write_len = 0;
        t->stat = OCTEON_TWSI_STAT_START;
    } else if (value & OCTEON_TWSI_CTL_STP) {
        t->have_slave = false;
        t->addr_phase = false;
        t->stat = OCTEON_TWSI_STAT_IDLE;
    } else if (t->addr_phase) {
        eeprom = octeon_twsi_find_spd(t);
        t->addr_phase = false;
        if (eeprom) {
            t->stat = t->read_transfer ? OCTEON_TWSI_STAT_RXADDR_ACK :
                                         OCTEON_TWSI_STAT_TXADDR_ACK;
        } else {
            t->stat = t->read_transfer ? OCTEON_TWSI_STAT_RXADDR_NAK :
                                         OCTEON_TWSI_STAT_TXADDR_NAK;
        }
    } else if (t->read_transfer) {
        t->stat = (value & OCTEON_TWSI_CTL_AAK) ?
                  OCTEON_TWSI_STAT_RXDATA_ACK : OCTEON_TWSI_STAT_RXDATA_NAK;
    } else {
        t->stat = octeon_twsi_find_spd(t) ? OCTEON_TWSI_STAT_TXDATA_ACK :
                                            OCTEON_TWSI_STAT_TXDATA_NAK;
    }
}

static void octeon_twsi_write_data(OcteonTWSIState *t, uint8_t value)
{
    OcteonSpdEepromState *eeprom;

    t->data = value;

    if (!t->have_slave) {
        t->slave_addr = value >> 1;
        t->read_transfer = value & 1;
        t->have_slave = true;
        t->addr_phase = true;
        t->write_len = 0;
        return;
    }

    if (!t->read_transfer && t->write_len < ARRAY_SIZE(t->write_buf)) {
        t->write_buf[t->write_len++] = value;
        eeprom = octeon_twsi_find_spd(t);
        if (eeprom) {
            if (t->write_len == 1) {
                eeprom->offset = value;
            } else if (t->write_len == ARRAY_SIZE(t->write_buf)) {
                eeprom->offset = (t->write_buf[0] << 8) | t->write_buf[1];
            }
        }
    }
}

static uint8_t octeon_twsi_read_reg(OcteonTWSIState *t, unsigned int reg)
{
    switch (reg) {
    case OCTEON_TWSI_EOP_DATA:
        if (t->read_transfer) {
            OcteonSpdEepromState *eeprom = octeon_twsi_find_spd(t);

            t->data = eeprom ? octeon_spd_eeprom_read(eeprom) : 0xff;
        }
        return t->data;
    case OCTEON_TWSI_EOP_CTL:
        return t->ctl;
    case OCTEON_TWSI_EOP_STAT:
        return t->stat;
    default:
        return 0;
    }
}

static void octeon_twsi_write_reg(OcteonTWSIState *t, unsigned int reg,
                                  uint8_t value)
{
    switch (reg) {
    case OCTEON_TWSI_EOP_DATA:
        octeon_twsi_write_data(t, value);
        break;
    case OCTEON_TWSI_EOP_CTL:
        octeon_twsi_write_ctl(t, value);
        break;
    default:
        break;
    }
}

static uint64_t octeon_twsi_exec(OcteonTWSIState *t, uint64_t value)
{
    unsigned int op = (value >> OCTEON_TWSI_SW_OP_SHIFT) & 0xf;
    unsigned int reg = (value >> OCTEON_TWSI_SW_EOP_IA_SHIFT) & 0x7;

    if (!(value & OCTEON_TWSI_SW_V)) {
        return value;
    }

    if (op == OCTEON_TWSI_OP_EOP_IA) {
        if (value & OCTEON_TWSI_SW_R) {
            value &= ~OCTEON_TWSI_SW_DATA_MASK;
            value |= octeon_twsi_read_reg(t, reg);
        } else {
            octeon_twsi_write_reg(t, reg, value & 0xff);
        }
    }

    return value & ~OCTEON_TWSI_SW_V;
}

static uint64_t octeon_twsi_read(void *opaque, hwaddr addr, unsigned size)
{
    OcteonTWSIState *t = opaque;
    hwaddr reg = addr & ~7ULL;

    switch (reg) {
    case OCTEON_TWSI_SW_TWSI:
        return octeon_read64(t->sw_twsi, addr, size);
    case OCTEON_TWSI_INT:
        return octeon_read64(t->int_reg, addr, size);
    default:
        return 0;
    }
}

static void octeon_twsi_write(void *opaque, hwaddr addr,
                              uint64_t value, unsigned size)
{
    OcteonTWSIState *t = opaque;
    hwaddr reg = addr & ~7ULL;
    uint64_t old = 0;

    switch (reg) {
    case OCTEON_TWSI_SW_TWSI:
        old = t->sw_twsi;
        value = octeon_write64(old, addr, value, size);
        if (size == 4 && !(addr & 4)) {
            t->sw_twsi = value;
        } else {
            t->sw_twsi = octeon_twsi_exec(t, value);
        }
        break;
    case OCTEON_TWSI_INT:
        old = t->int_reg;
        t->int_reg = octeon_write64(old, addr, value, size);
        break;
    default:
        break;
    }
}

static const MemoryRegionOps octeon_twsi_ops = {
    .read = octeon_twsi_read,
    .write = octeon_twsi_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
    .valid = {
        .min_access_size = 4,
        .max_access_size = 8,
    },
};

static void octeon_irq_set(void *opaque, int irq, int level)
{
    OcteonState *s = opaque;
    unsigned int src;

    switch (irq) {
    case OCTEON_IRQ_UART:
        src = OCTEON_CIU3_SRC_UART0;
        if (!octeon_ciu3_set_level(s, src, level)) {
            return;
        }
        qatomic_set(&s->uart_pending, level);
        break;
    case OCTEON_IRQ_PCI:
        src = OCTEON_CIU3_SRC_PCI_INTA;
        if (!octeon_ciu3_set_level(s, src, level)) {
            return;
        }
        qatomic_set(&s->pci_pending, level);
        break;
    case OCTEON_IRQ_USB0:
        src = OCTEON_CIU3_SRC_USB0;
        if (!octeon_ciu3_set_level(s, src, level)) {
            return;
        }
        break;
    case OCTEON_IRQ_USB1:
        src = OCTEON_CIU3_SRC_USB1;
        if (!octeon_ciu3_set_level(s, src, level)) {
            return;
        }
        break;
    default:
        return;
    }

    octeon_intc_update_cpu_mask(s, octeon_ciu3_source_cpu_mask(s, src));
}

static uint64_t octeon_pci_absent(unsigned size)
{
    return size == 1 ? 0xff : size == 2 ? 0xffff : 0xffffffffU;
}

static bool octeon_pcie_config_addr(hwaddr addr, uint32_t *pci_addr,
                                    bool sli_cfg)
{
    unsigned int bus = (addr >> 20) & 0xff;
    unsigned int dev = (addr >> 15) & 0x1f;
    unsigned int fn = (addr >> 12) & 0x7;
    unsigned int reg = addr & 0xfff;

    if (sli_cfg) {
        if (addr >= OCTEON_PCIE_SLI_CFG_PORT_SIZE ||
            bus != 0 || dev != 0 || fn != 0 ||
            reg >= PCI_CONFIG_SPACE_SIZE) {
            return false;
        }
    } else if (bus != 1 || dev != 0 || fn != 0 ||
               reg >= PCI_CONFIG_SPACE_SIZE) {
        return false;
    }

    *pci_addr = 0x80000000U | (OCTEON_PCIE0_ENDPOINT_DEVFN << 8) | reg;
    return true;
}

static uint64_t octeon_pcie_cfg_read(void *opaque, hwaddr addr, unsigned size)
{
    OcteonState *s = opaque;
    uint32_t pci_addr;

    if (!octeon_pcie_config_addr(addr, &pci_addr, false)) {
        return octeon_pci_absent(size);
    }

    return pci_data_read(s->pci_bus, pci_addr, size);
}

static void octeon_pcie_cfg_write(void *opaque, hwaddr addr,
                                  uint64_t value, unsigned size)
{
    OcteonState *s = opaque;
    uint32_t pci_addr;

    if (octeon_pcie_config_addr(addr, &pci_addr, false)) {
        pci_data_write(s->pci_bus, pci_addr, value, size);
    }
}

static const MemoryRegionOps octeon_pcie_cfg_ops = {
    .read = octeon_pcie_cfg_read,
    .write = octeon_pcie_cfg_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = {
        .min_access_size = 1,
        .max_access_size = 4,
    },
};

static uint64_t octeon_pcie_sli_cfg_read(void *opaque, hwaddr addr,
                                         unsigned size)
{
    OcteonState *s = opaque;
    uint32_t pci_addr;

    if (!octeon_pcie_config_addr(addr, &pci_addr, true)) {
        return octeon_pci_absent(size);
    }

    return pci_data_read(s->pci_bus, pci_addr, size);
}

static void octeon_pcie_sli_cfg_write(void *opaque, hwaddr addr,
                                      uint64_t value, unsigned size)
{
    OcteonState *s = opaque;
    uint32_t pci_addr;

    if (octeon_pcie_config_addr(addr, &pci_addr, true)) {
        pci_data_write(s->pci_bus, pci_addr, value, size);
    }
}

static const MemoryRegionOps octeon_pcie_sli_cfg_ops = {
    .read = octeon_pcie_sli_cfg_read,
    .write = octeon_pcie_sli_cfg_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
    .valid = {
        .min_access_size = 1,
        .max_access_size = 4,
    },
};

static uint64_t octeon_pcie_bswap(uint64_t value, unsigned size)
{
    switch (size) {
    case 1:
        return value;
    case 2:
        return bswap16(value);
    case 4:
        return bswap32(value);
    case 8:
        return bswap64(value);
    default:
        g_assert_not_reached();
    }
}

static uint64_t octeon_pcie_mem_subid_reg(hwaddr addr)
{
    unsigned int subid = (addr >> 28) & 3;

    return OCTEON_PEXP_BASE + OCTEON_SLI_MEM_ACCESS_SUBID_BASE +
           subid * OCTEON_SLI_MEM_ACCESS_SUBID_STRIDE;
}

static bool octeon_pcie_mem_swap_enabled(OcteonState *s, hwaddr addr,
                                         bool write)
{
    uint64_t value;
    uint64_t mask = write ? OCTEON_SLI_MEM_ACCESS_SUBID_ESW_M :
                            OCTEON_SLI_MEM_ACCESS_SUBID_ESR_M;
    unsigned int shift = write ? OCTEON_SLI_MEM_ACCESS_SUBID_ESW_S :
                                 OCTEON_SLI_MEM_ACCESS_SUBID_ESR_S;

    if (!octeon_csr_lookup(s, octeon_pcie_mem_subid_reg(addr), &value)) {
        return false;
    }

    return ((value & mask) >> shift) == OCTEON_SLI_MEM_ACCESS_SWAP;
}

static uint64_t octeon_pcie0_mem_read(void *opaque, hwaddr addr,
                                      unsigned size)
{
    OcteonState *s = opaque;
    uint64_t value;

    switch (size) {
    case 1:
        value = address_space_ldub(&s->pcie0_mem_as, addr,
                                   MEMTXATTRS_UNSPECIFIED, NULL);
        break;
    case 2:
        value = address_space_lduw_le(&s->pcie0_mem_as, addr,
                                      MEMTXATTRS_UNSPECIFIED, NULL);
        break;
    case 4:
        value = address_space_ldl_le(&s->pcie0_mem_as, addr,
                                     MEMTXATTRS_UNSPECIFIED, NULL);
        break;
    case 8:
        value = address_space_ldq_le(&s->pcie0_mem_as, addr,
                                     MEMTXATTRS_UNSPECIFIED, NULL);
        break;
    default:
        g_assert_not_reached();
    }

    if (TARGET_BIG_ENDIAN && !octeon_pcie_mem_swap_enabled(s, addr, false)) {
        value = octeon_pcie_bswap(value, size);
    }

    return value;
}

static void octeon_pcie0_mem_write(void *opaque, hwaddr addr,
                                   uint64_t value, unsigned size)
{
    OcteonState *s = opaque;

    if (TARGET_BIG_ENDIAN && !octeon_pcie_mem_swap_enabled(s, addr, true)) {
        value = octeon_pcie_bswap(value, size);
    }

    switch (size) {
    case 1:
        address_space_stb(&s->pcie0_mem_as, addr, value,
                          MEMTXATTRS_UNSPECIFIED, NULL);
        break;
    case 2:
        address_space_stw_le(&s->pcie0_mem_as, addr, value,
                             MEMTXATTRS_UNSPECIFIED, NULL);
        break;
    case 4:
        address_space_stl_le(&s->pcie0_mem_as, addr, value,
                             MEMTXATTRS_UNSPECIFIED, NULL);
        break;
    case 8:
        address_space_stq_le(&s->pcie0_mem_as, addr, value,
                             MEMTXATTRS_UNSPECIFIED, NULL);
        break;
    default:
        g_assert_not_reached();
    }
}

static const MemoryRegionOps octeon_pcie0_mem_ops = {
    .read = octeon_pcie0_mem_read,
    .write = octeon_pcie0_mem_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
    .valid = {
        .min_access_size = 1,
        .max_access_size = 8,
    },
    .impl = {
        .min_access_size = 1,
        .max_access_size = 8,
    },
};

static int octeon_pci_map_irq(PCIDevice *pci_dev, int irq_num)
{
    return 0;
}

static void octeon_pci_set_irq(void *opaque, int irq_num, int level)
{
    octeon_irq_set(opaque, OCTEON_IRQ_PCI, level);
}

static void octeon_pci_host_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->fw_name = "pci";
    dc->user_creatable = false;
}

static const TypeInfo octeon_pci_host_info = {
    .name          = TYPE_OCTEON_PCI_HOST,
    .parent        = TYPE_PCI_HOST_BRIDGE,
    .instance_size = sizeof(OcteonPCIHostState),
    .class_init    = octeon_pci_host_class_init,
};

static void octeon_pci_host_register_types(void)
{
    type_register_static(&octeon_pci_host_info);
}

type_init(octeon_pci_host_register_types)

static void octeon_cpu_reset(void *opaque)
{
    OcteonCPUState *cs = opaque;
    CPUMIPSState *env = &cs->cpu->env;
    CPUState *cpu = CPU(cs->cpu);

    cpu_reset(cpu);

    env->CP0_CvmMemCtl = 0x100;
    env->CP0_CvmCtl = 0;

    if (!cs->boot_cpu) {
        /* Secondary cores are released by the Octeon reset controller. */
        octeon_cpu_park_now(cs->cpu);
        return;
    }

    if (cs->board->firmware_entry) {
        env->active_tc.PC = cs->board->firmware_entry;
    }
}

static void octeon_create_cpus(OcteonState *s)
{
    unsigned int i;

    s->cpuclk = clock_new(OBJECT(s->machine), "cpu-refclk");
    clock_set_hz(s->cpuclk, s->cpu_hz);
    s->cpu_count = s->machine->smp.cpus;
    qatomic_set(&s->ciu3_pp_rst, octeon_secondary_cpu_mask(s));

    for (i = 0; i < s->cpu_count; i++) {
        DeviceState *dev = qdev_new(s->machine->cpu_type);

        s->cpu[i].board = s;
        s->cpu[i].boot_cpu = i == 0;
        object_property_set_bool(OBJECT(dev), "big-endian", TARGET_BIG_ENDIAN,
                                 &error_abort);
        object_property_set_bool(OBJECT(dev), "start-powered-off", i != 0,
                                 &error_abort);
        qdev_connect_clock_in(dev, "clk-in", s->cpuclk);
        qdev_realize(dev, NULL, &error_abort);
        s->cpu[i].cpu = MIPS_CPU(dev);

        cpu_mips_irq_init_cpu(s->cpu[i].cpu);
        cpu_mips_clock_init(s->cpu[i].cpu);
        qemu_register_reset(octeon_cpu_reset, &s->cpu[i]);
    }
}

static uint64_t octeon_map_ram_alias(OcteonState *s, MemoryRegion *alias,
                                     const char *name, hwaddr base,
                                     uint64_t offset, uint64_t size)
{
    MemoryRegion *sysmem = get_system_memory();

    if (!size) {
        return offset;
    }

    memory_region_init_alias(alias, NULL, name, s->machine->ram, offset, size);
    memory_region_add_subregion(sysmem, base, alias);
    return offset + size;
}

static void octeon_map_ram(OcteonState *s)
{
    MemoryRegion *sysmem = get_system_memory();
    uint64_t offset = 0;
    uint64_t remaining = s->machine->ram_size;
    uint64_t size;

    size = MIN(remaining, (uint64_t)OCTEON_DR0_SIZE);
    offset = octeon_map_ram_alias(s, &s->dr0, "octeon.dr0",
                                  OCTEON_DR0_BASE, offset, size);
    remaining -= size;

    size = MIN(remaining, (uint64_t)OCTEON_DR1_SIZE);
    octeon_map_ram_alias(s, &s->dr1, "octeon.dr1",
                         OCTEON_DR1_BASE, offset, size);

    memory_region_init_ram(&s->cvmseg, NULL, "octeon.cvmseg",
                           OCTEON_CVMSEG_TOTAL_SIZE, &error_fatal);
    memory_region_add_subregion(sysmem, OCTEON_CVMSEG_BASE, &s->cvmseg);
}

static void octeon_load_firmware(OcteonState *s)
{
    g_autofree char *filename = NULL;
    g_autofree gchar *contents = NULL;
    g_autoptr(GError) err = NULL;
    gsize len;
    uint8_t *flash;

    if (!s->machine->firmware) {
        return;
    }
    if (!s->flash) {
        error_report("Octeon boot flash is not initialized");
        exit(1);
    }

    filename = qemu_find_file(QEMU_FILE_TYPE_BIOS, s->machine->firmware);
    if (!filename) {
        error_report("Could not find Octeon firmware '%s'",
                     s->machine->firmware);
        exit(1);
    }

    if (!g_file_get_contents(filename, &contents, &len, &err)) {
        error_report("Could not load Octeon firmware '%s': %s",
                     filename, err->message);
        exit(1);
    }

    if (len == 0 || len > OCTEON_FLASH_ENV_OFFSET) {
        error_report("Octeon firmware '%s' has invalid size %" G_GSIZE_FORMAT,
                     filename, len);
        exit(1);
    }

    flash = memory_region_get_ram_ptr(s->flash);
    memcpy(flash, contents, len);
    s->firmware_entry = cpu_mips_phys_to_kseg1(NULL, OCTEON_FLASH_RESET_BASE);
}

static MemTxResult octeon_boot_flash_read(void *opaque, hwaddr addr,
                                          uint64_t *data, unsigned size,
                                          MemTxAttrs attrs)
{
    OcteonState *s = opaque;
    uint8_t buf[8];
    MemTxResult result = MEMTX_OK;
    unsigned int i;

    for (i = 0; i < size; i++) {
        uint64_t value;

        result |= memory_region_dispatch_read(s->flash, addr + i, &value,
                                              MO_8, attrs);
        buf[i] = value;
    }
    *data = ldn_be_p(buf, size);
    return result;
}

static MemTxResult octeon_boot_flash_write(void *opaque, hwaddr addr,
                                           uint64_t value, unsigned size,
                                           MemTxAttrs attrs)
{
    OcteonState *s = opaque;
    unsigned access_size = MIN(size, 4);

    if (size > access_size) {
        access_size = OCTEON_FLASH_WIDTH;
    }
    return memory_region_dispatch_write(s->flash, addr, value,
                                        size_memop(access_size) | MO_BE,
                                        attrs);
}

static const MemoryRegionOps octeon_boot_flash_ops = {
    .read_with_attrs = octeon_boot_flash_read,
    .write_with_attrs = octeon_boot_flash_write,
    .endianness = DEVICE_BIG_ENDIAN,
    .valid = {
        .min_access_size = 1,
        .max_access_size = 8,
    },
};

static void octeon_init_flash(OcteonState *s)
{
    DriveInfo *dinfo = drive_get(IF_PFLASH, 0, 0);
    DeviceState *dev;
    uint8_t *flash;

    dev = qdev_new(TYPE_PFLASH_CFI02);
    if (dinfo) {
        qdev_prop_set_drive(dev, "drive", blk_by_legacy_dinfo(dinfo));
    }
    qdev_prop_set_uint32(dev, "num-blocks0", OCTEON_FLASH_MAIN_SECTORS);
    qdev_prop_set_uint32(dev, "sector-length0",
                         OCTEON_FLASH_MAIN_SECTOR_SIZE);
    qdev_prop_set_uint32(dev, "num-blocks1", OCTEON_FLASH_ENV_SECTORS);
    qdev_prop_set_uint32(dev, "sector-length1",
                         OCTEON_FLASH_ENV_SECTOR_SIZE);
    qdev_prop_set_uint8(dev, "width", OCTEON_FLASH_WIDTH);
    qdev_prop_set_uint8(dev, "mappings", 1);
    qdev_prop_set_uint8(dev, "big-endian", TARGET_BIG_ENDIAN);
    qdev_prop_set_uint16(dev, "id0", 0x01);
    qdev_prop_set_uint16(dev, "id1", 0x7e);
    qdev_prop_set_uint16(dev, "id2", 0x00);
    qdev_prop_set_uint16(dev, "id3", 0x00);
    qdev_prop_set_uint16(dev, "unlock-addr0", 0x555);
    qdev_prop_set_uint16(dev, "unlock-addr1", 0x2aa);
    qdev_prop_set_string(dev, "name", "octeon.bootbus-flash");
    sysbus_realize_and_unref(SYS_BUS_DEVICE(dev), &error_fatal);

    s->flash = sysbus_mmio_get_region(SYS_BUS_DEVICE(dev), 0);
    if (!dinfo) {
        flash = memory_region_get_ram_ptr(s->flash);
        memset(flash, 0xff, OCTEON_FLASH_SIZE);
    }
    memory_region_init_io(&s->boot_flash, NULL, &octeon_boot_flash_ops, s,
                          "octeon.bootbus-flash-window", OCTEON_FLASH_SIZE);
    memory_region_add_subregion(get_system_memory(), OCTEON_FLASH_BASE,
                                &s->boot_flash);

    memory_region_init_alias(&s->boot_flash_alias, NULL,
                             "octeon.bootbus-flash-reset-alias",
                             &s->boot_flash, 0, OCTEON_FLASH_RESET_SIZE);
    memory_region_add_subregion(get_system_memory(), OCTEON_FLASH_RESET_BASE,
                                &s->boot_flash_alias);
}

static void octeon_init_mio_boot_loc(OcteonState *s)
{
    memory_region_init_ram(&s->bootbus_led, NULL, "octeon.bootbus-led",
                           OCTEON_BOOTBUS_LED_SIZE, &error_fatal);
    memory_region_init_ram_ptr(&s->mio_boot_loc, NULL,
                               "octeon.mio-boot-loc",
                               OCTEON_MIO_BOOT_LOC_SIZE,
                               s->mio_boot_loc_data);
}

static void octeon_init_pci(OcteonState *s)
{
    DeviceState *dev;
    OcteonPCIHostState *host;
    PCIHostState *phb;

    dev = qdev_new(TYPE_OCTEON_PCI_HOST);
    host = OCTEON_PCI_HOST(dev);
    phb = PCI_HOST_BRIDGE(dev);

    memory_region_init(&host->mem, OBJECT(dev), "octeon.pci-mem",
                       OCTEON_PCIE0_MEM_SIZE);
    memory_region_init(&host->io, OBJECT(dev), "octeon.pci-io",
                       OCTEON_PCIE0_IO_SIZE);

    phb->bus = pci_root_bus_new(dev, "pci", &host->mem, &host->io,
                                OCTEON_PCIE0_ENDPOINT_DEVFN, TYPE_PCI_BUS);
    s->pci_bus = phb->bus;
    pci_bus_irqs(s->pci_bus, octeon_pci_set_irq, s, 1);
    pci_bus_map_irqs(s->pci_bus, octeon_pci_map_irq);
    sysbus_realize_and_unref(SYS_BUS_DEVICE(dev), &error_fatal);

    memory_region_init_io(&s->pcie_cfg, NULL, &octeon_pcie_cfg_ops, s,
                          "octeon.pcie-config", OCTEON_PCIE_CFG_SIZE);
    memory_region_add_subregion(get_system_memory(), OCTEON_PCIE_CFG_BASE,
                                &s->pcie_cfg);

    memory_region_init_io(&s->pcie_sli_cfg, NULL, &octeon_pcie_sli_cfg_ops,
                          s, "octeon.pcie-sli-config",
                          OCTEON_PCIE_SLI_CFG_SIZE);
    memory_region_add_subregion(get_system_memory(),
                                OCTEON_PCIE_SLI_CFG_BASE,
                                &s->pcie_sli_cfg);

    memory_region_init_alias(&s->pcie0_io, NULL, "octeon.pcie0-io",
                             &host->io, 0,
                             OCTEON_PCIE0_IO_SIZE);
    memory_region_add_subregion(get_system_memory(), OCTEON_PCIE0_IO_BASE,
                                &s->pcie0_io);

    address_space_init(&s->pcie0_mem_as, &host->mem,
                       "octeon.pcie0-mem-as");
    memory_region_init_io(&s->pcie0_mem, NULL, &octeon_pcie0_mem_ops, s,
                          "octeon.pcie0-mem", OCTEON_PCIE0_MEM_SIZE);
    memory_region_add_subregion(get_system_memory(), OCTEON_PCIE0_MEM_BASE,
                                &s->pcie0_mem);
}

static uint64_t octeon_uart_tx_read(void *opaque, hwaddr addr, unsigned size)
{
    return 0;
}

static void octeon_uart_tx_write(void *opaque, hwaddr addr,
                                 uint64_t value, unsigned size)
{
    OcteonState *s = opaque;

    if (size == OCTEON_CSR_32BIT_SIZE &&
        addr == OCTEON_UART_TX_HI32_OFFSET) {
        return;
    }

    serial_io_ops.write(&s->uart->serial, 0, value & 0xff, 1);
}

static const MemoryRegionOps octeon_uart_tx_ops = {
    .read = octeon_uart_tx_read,
    .write = octeon_uart_tx_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
    .valid = {
        .min_access_size = 1,
        .max_access_size = 8,
    },
};

static void octeon_init_uart(OcteonState *s)
{
    SerialMM *uart;
    qemu_irq uart_irq;
    MemoryRegion *uart_mmio;
    int baudbase = s->io_hz / 16;

    uart_irq = qemu_allocate_irq(octeon_irq_set, s, OCTEON_IRQ_UART);
    uart = serial_mm_init(get_system_memory(), OCTEON_UART0_BASE, 3, uart_irq,
                          baudbase, serial_hd(0), DEVICE_NATIVE_ENDIAN);
    s->uart = uart;

    uart_mmio = sysbus_mmio_get_region(SYS_BUS_DEVICE(uart), 0);
    memory_region_init_alias(&s->uart_alias, NULL, "octeon.uart0-alias",
                             uart_mmio, 0, memory_region_size(uart_mmio));
    memory_region_add_subregion(get_system_memory(), OCTEON_UART0_ALIAS_BASE,
                                &s->uart_alias);

    memory_region_init_io(&s->uart_tx, NULL, &octeon_uart_tx_ops, s,
                          "octeon.uart0-tx", OCTEON_UART_TX_SIZE);
    memory_region_add_subregion(get_system_memory(),
                                OCTEON_UART0_BASE + OCTEON_UART_TX_REG,
                                &s->uart_tx);
    memory_region_init_io(&s->uart_alias_tx, NULL, &octeon_uart_tx_ops, s,
                          "octeon.uart0-alias-tx", OCTEON_UART_TX_SIZE);
    memory_region_add_subregion(get_system_memory(),
                                OCTEON_UART0_ALIAS_BASE + OCTEON_UART_TX_REG,
                                &s->uart_alias_tx);
}

static void octeon_init_twsi(OcteonState *s)
{
    static const uint64_t base[OCTEON_TWSI_COUNT] = {
        OCTEON_TWSI0_BASE,
        OCTEON_TWSI1_BASE,
    };
    static const char *const name[OCTEON_TWSI_COUNT] = {
        "octeon.twsi0",
        "octeon.twsi1",
    };
    unsigned int i;

    for (i = 0; i < OCTEON_TWSI_COUNT; i++) {
        s->twsi[i].board = s;
        s->twsi[i].bus = i;
        s->twsi[i].stat = OCTEON_TWSI_STAT_IDLE;

        memory_region_init_io(&s->twsi[i].mmio, NULL, &octeon_twsi_ops,
                              &s->twsi[i], name[i], OCTEON_TWSI_SIZE);
        memory_region_add_subregion(get_system_memory(), base[i],
                                    &s->twsi[i].mmio);
    }
}

static void octeon_init_usb(OcteonState *s)
{
    static const uint64_t uctl_base[OCTEON_USB_COUNT] = {
        OCTEON_USB0_UCTL_BASE,
        OCTEON_USB1_UCTL_BASE,
    };
    static const uint64_t dwc3_base[OCTEON_USB_COUNT] = {
        OCTEON_USB0_DWC3_BASE,
        OCTEON_USB1_DWC3_BASE,
    };
    static const char * const uctl_name[OCTEON_USB_COUNT] = {
        "octeon.usb0-uctl",
        "octeon.usb1-uctl",
    };
    static const char * const dwc3_window_name[OCTEON_USB_COUNT] = {
        "octeon.usb0-dwc3-window",
        "octeon.usb1-dwc3-window",
    };
    static const char * const dwc3_as_name[OCTEON_USB_COUNT] = {
        "octeon.usb0-dwc3-as",
        "octeon.usb1-dwc3-as",
    };
    static const OcteonIRQ irq[OCTEON_USB_COUNT] = {
        OCTEON_IRQ_USB0,
        OCTEON_IRQ_USB1,
    };
    unsigned int i;

    for (i = 0; i < OCTEON_USB_COUNT; i++) {
        OcteonUsbState *usb = &s->usb[i];
        DeviceState *dev = qdev_new(TYPE_USB_DWC3);
        USBDWC3 *dwc3 = USB_DWC3(dev);
        MemoryRegion *dwc3_mr;

        usb->board = s;
        usb->dwc3 = dwc3;
        usb->index = i;
        usb->regs = g_hash_table_new_full(octeon_uint64_hash,
                                           octeon_uint64_equal,
                                           g_free, g_free);
        memory_region_init_io(&usb->uctl, NULL, &octeon_uctl_ops, usb,
                              uctl_name[i], OCTEON_UCTL_SIZE);
        memory_region_add_subregion(get_system_memory(), uctl_base[i],
                                    &usb->uctl);

        object_property_set_link(OBJECT(&dwc3->sysbus_xhci), "dma",
                                 OBJECT(get_system_memory()), &error_abort);
        qdev_prop_set_uint32(DEVICE(&dwc3->sysbus_xhci), "intrs", 1);
        sysbus_realize_and_unref(SYS_BUS_DEVICE(dev), &error_fatal);
        dwc3_mr = sysbus_mmio_get_region(SYS_BUS_DEVICE(dev), 0);
        address_space_init(&usb->dwc3_as, dwc3_mr, dwc3_as_name[i]);
        memory_region_init_io(&usb->dwc3_window, NULL,
                              &octeon_dwc3_window_ops, usb,
                              dwc3_window_name[i], DWC3_SIZE);
        memory_region_add_subregion(get_system_memory(), dwc3_base[i],
                                    &usb->dwc3_window);
        sysbus_connect_irq(SYS_BUS_DEVICE(&dwc3->sysbus_xhci), 0,
                           qemu_allocate_irq(octeon_irq_set, s, irq[i]));
    }
}

static void octeon_board_reset(void *opaque)
{
    OcteonState *s = opaque;
    unsigned int i;

    if (s->bootbus_led_mapped) {
        memory_region_del_subregion(get_system_memory(), &s->bootbus_led);
        s->bootbus_led_mapped = false;
    }
    memset(s->mio_boot_reg_cfg, 0, sizeof(s->mio_boot_reg_cfg));
    s->mio_boot_reg_cfg[0] = OCTEON_MIO_BOOT_REG_CFG_RESET_BASE;
    s->mio_boot_loc_cfg[0] = 0;
    s->mio_boot_loc_cfg[1] = 0;
    s->mio_boot_loc_adr = 0;
    s->mio_fus_rcmd = 0;
    s->mio_emm_rsp_sts = 0;
    s->mio_emm_rsp_lo = 0;
    s->mio_emm_rsp_hi = 0;
    s->mio_emm_int = 0;
    s->mio_emm_dma_int = 0;
    s->mio_emm_buf_idx = 0;
    s->mio_emm_buf_inc = false;
    memset(s->mio_emm_buf, 0, sizeof(s->mio_emm_buf));
    s->rst_pp_power = octeon_secondary_cpu_mask(s);
    memset(s->mio_boot_loc_data, 0, sizeof(s->mio_boot_loc_data));
    octeon_mio_boot_loc_update(s);

    s->pcie_cfg_rd_addr = 0;
    memset(s->ciu_mbox, 0, sizeof(s->ciu_mbox));
    memset(s->ciu_ipi_en, 0, sizeof(s->ciu_ipi_en));
    qatomic_set(&s->ciu_gpio_rx, OCTEON_CIU_GPIO_INPUTS);
    qatomic_set(&s->ciu_gpio_tx, 0);
    memset(s->ciu_gpio_bit_cfg, 0, sizeof(s->ciu_gpio_bit_cfg));
    qatomic_set(&s->uart_pending, false);
    qatomic_set(&s->pci_pending, false);
    qatomic_set(&s->ciu3_pp_rst, octeon_secondary_cpu_mask(s));
    memset(s->ciu3_src_state, 0, sizeof(s->ciu3_src_state));
    memset(s->ciu3_src_ctl, 0, sizeof(s->ciu3_src_ctl));
    memset(s->ciu3_idt_ctl, 0, sizeof(s->ciu3_idt_ctl));
    memset(s->ciu3_idt_pp, 0, sizeof(s->ciu3_idt_pp));
    memset(s->ciu3_idt_io, 0, sizeof(s->ciu3_idt_io));
    memset(s->ciu3_src_cursor, 0, sizeof(s->ciu3_src_cursor));
    memset(s->ciu3_irq_line, 0, sizeof(s->ciu3_irq_line));

    for (i = 0; i < OCTEON_LMC_COUNT; i++) {
        g_hash_table_remove_all(s->lmc[i].regs);
    }
    for (i = 0; i < OCTEON_TWSI_COUNT; i++) {
        OcteonTWSIState *t = &s->twsi[i];

        t->sw_twsi = 0;
        t->int_reg = 0;
        t->ctl = 0;
        t->stat = OCTEON_TWSI_STAT_IDLE;
        t->data = 0;
        t->slave_addr = 0;
        t->write_len = 0;
        t->have_slave = false;
        t->read_transfer = false;
        t->addr_phase = false;
        memset(t->write_buf, 0, sizeof(t->write_buf));
    }
    for (i = 0; i < OCTEON_USB_COUNT; i++) {
        g_hash_table_remove_all(s->usb[i].regs);
    }
    for (i = 0; i < OCTEON_SPD_EEPROM_COUNT; i++) {
        s->spd[i].offset = 0;
    }

    g_hash_table_remove_all(s->csr_values);
    octeon_intc_update_all_cpus(s);
}

static void mips_octeon_init(MachineState *machine)
{
    OcteonMachineState *oms = OCTEON_MACHINE(machine);
    OcteonState *s = g_new0(OcteonState, 1);

    octeon_validate_clocks(oms);

    s->machine = machine;
    s->cpu_hz = oms->cpu_hz;
    s->ref_hz = oms->ref_hz;
    s->io_hz = oms->io_hz;
    s->ddr_hz = oms->ddr_hz;
    s->csr_values = g_hash_table_new_full(octeon_uint64_hash,
                                          octeon_uint64_equal,
                                          g_free, g_free);

    if (machine->kernel_filename) {
        error_report("-kernel is not implemented for octeon3; "
                     "boot via -bios and U-Boot");
        exit(1);
    }

    octeon_init_lmcs(s);
    octeon_validate_ram_size(machine->ram_size);
    octeon_init_spd(s);
    octeon_map_ram(s);
    octeon_init_flash(s);
    octeon_init_mio_boot_loc(s);
    octeon_load_firmware(s);
    octeon_create_cpus(s);

    memory_region_init_io(&s->ciu, NULL, &octeon_ciu_ops, s,
                          "octeon.ciu", OCTEON_CIU_SIZE);
    memory_region_add_subregion(get_system_memory(), OCTEON_CIU_BASE,
                                &s->ciu);

    memory_region_init_io(&s->ciu3, NULL, &octeon_ciu3_ops, s,
                          "octeon.ciu3", OCTEON_CIU3_SIZE);
    memory_region_add_subregion(get_system_memory(), OCTEON_CIU3_BASE,
                                &s->ciu3);

    octeon_init_uart(s);
    octeon_init_twsi(s);

    memory_region_init_io(&s->csr, NULL, &octeon_csr_ops, s,
                          "octeon.csr", OCTEON_CSR_SIZE);
    memory_region_add_subregion_overlap(get_system_memory(), OCTEON_CSR_BASE,
                                        &s->csr, -1);

    memory_region_init_io(&s->pexp, NULL, &octeon_pexp_ops, s,
                          "octeon.pexp", OCTEON_PEXP_SIZE);
    memory_region_add_subregion(get_system_memory(), OCTEON_PEXP_BASE,
                                &s->pexp);

    memory_region_init_io(&s->dpi, NULL, &octeon_dpi_ops, s,
                          "octeon.dpi", OCTEON_DPI_SIZE);
    memory_region_add_subregion(get_system_memory(), OCTEON_DPI_BASE,
                                &s->dpi);

    memory_region_init_io(&s->fpa, NULL, &octeon_fpa_ops, s,
                          "octeon.fpa", OCTEON_FPA_SIZE);
    memory_region_add_subregion(get_system_memory(), OCTEON_FPA_BASE,
                                &s->fpa);

    memory_region_init_io(&s->ipd, NULL, &octeon_ipd_ops, s,
                          "octeon.ipd", OCTEON_IPD_SIZE);
    memory_region_add_subregion(get_system_memory(), OCTEON_IPD_BASE,
                                &s->ipd);

    memory_region_init_io(&s->pko, NULL, &octeon_pko_ops, s,
                          "octeon.pko", OCTEON_PKO_SIZE);
    memory_region_add_subregion(get_system_memory(), OCTEON_PKO_BASE,
                                &s->pko);

    memory_region_init_io(&s->rng, NULL, &octeon_rng_ops, s,
                          "octeon.rng", OCTEON_RNG_SIZE);
    memory_region_add_subregion(get_system_memory(), OCTEON_RNG_BASE,
                                &s->rng);

    memory_region_init_io(&s->pow, NULL, &octeon_pow_ops, s,
                          "octeon.pow", OCTEON_POW_SIZE);
    memory_region_add_subregion(get_system_memory(), OCTEON_POW_BASE,
                                &s->pow);

    octeon_init_usb(s);
    octeon_init_pci(s);
    qemu_register_reset(octeon_board_reset, s);
}

static void octeon_machine_instance_init(Object *obj)
{
    OcteonMachineState *oms = OCTEON_MACHINE(obj);

    oms->cpu_hz = OCTEON_DEFAULT_CPU_HZ;
    oms->ref_hz = OCTEON_DEFAULT_REF_HZ;
    oms->io_hz = OCTEON_DEFAULT_IO_HZ;
    oms->ddr_hz = OCTEON_DEFAULT_DDR_HZ;

    object_property_add_uint64_ptr(obj, "cpu-clock-hz", &oms->cpu_hz,
                                   OBJ_PROP_FLAG_READWRITE);
    object_property_set_description(obj, "cpu-clock-hz",
                                    "CPU clock frequency in Hz");

    object_property_add_uint64_ptr(obj, "ref-clock-hz", &oms->ref_hz,
                                   OBJ_PROP_FLAG_READWRITE);
    object_property_set_description(obj, "ref-clock-hz",
                                    "Reference clock frequency in Hz");

    object_property_add_uint64_ptr(obj, "io-clock-hz", &oms->io_hz,
                                   OBJ_PROP_FLAG_READWRITE);
    object_property_set_description(obj, "io-clock-hz",
                                    "IO clock frequency in Hz");

    object_property_add_uint64_ptr(obj, "ddr-clock-hz", &oms->ddr_hz,
                                   OBJ_PROP_FLAG_READWRITE);
    object_property_set_description(obj, "ddr-clock-hz",
                                    "DDR clock frequency in Hz");
}

static void octeon_machine_class_init(ObjectClass *oc, const void *data)
{
    MachineClass *mc = MACHINE_CLASS(oc);

    mc->desc = "Cavium Octeon III / EBB7304 EVK";
    mc->init = mips_octeon_init;
    mc->default_cpu_type = MIPS_CPU_TYPE_NAME("OcteonCN73XX");
    mc->default_ram_size = OCTEON_DEFAULT_RAM_SIZE;
    mc->default_ram_id = "octeon.ram";
    mc->max_cpus = OCTEON_MAX_CPUS;
    mc->pci_allow_0_address = true;
}

static const TypeInfo octeon_machine_types[] = {
    {
        .name = TYPE_OCTEON_MACHINE,
        .parent = TYPE_MACHINE,
        .instance_size = sizeof(OcteonMachineState),
        .instance_init = octeon_machine_instance_init,
        .class_init = octeon_machine_class_init,
    }
};

DEFINE_TYPES(octeon_machine_types)
