/*
 * SPDX-License-Identifier: GPL-2.0+
 *
 * Andes custom CSRs bit definitions
 *
 */

/*
 * ========= Missing drafted/standard CSR definitions =========
 * TINFO is in official debug sepc, it's not in cpu_bits.h yet.
 */
#define CSR_TINFO           0x7a4

#if !defined(CONFIG_USER_ONLY)
/* ========= AndeStar V5 machine mode CSRs ========= */
/* Configuration Registers */
#define CSR_MICM_CFG            0xfc0
#define CSR_MDCM_CFG            0xfc1
#define CSR_MMSC_CFG            0xfc2
#define CSR_MMSC_CFG2           0xfc3
#define CSR_MVEC_CFG            0xfc7

/* Crash Debug CSRs */
#define CSR_MCRASH_STATESAVE    0xfc8
#define CSR_MSTATUS_CRASHSAVE   0xfc9

/* Memory CSRs */
#define CSR_MILMB               0x7c0
#define CSR_MDLMB               0x7c1
#define CSR_MECC_CODE           0x7C2
#define CSR_MNVEC               0x7c3
#define CSR_MCACHE_CTL          0x7ca
#define CSR_MCCTLBEGINADDR      0x7cb
#define CSR_MCCTLCOMMAND        0x7cc
#define CSR_MCCTLDATA           0x7cd
#define CSR_MPPIB               0x7f0
#define CSR_MFIOB               0x7f1

/* Hardware Stack Protection & Recording */
#define CSR_MHSP_CTL            0x7c6
#define CSR_MSP_BOUND           0x7c7
#define CSR_MSP_BASE            0x7c8
#define CSR_MXSTATUS            0x7c4
#define CSR_MDCAUSE             0x7c9
#define CSR_MSLIDELEG           0x7d5
#define CSR_MSAVESTATUS         0x7d6
#define CSR_MSAVEEPC1           0x7d7
#define CSR_MSAVECAUSE1         0x7d8
#define CSR_MSAVEEPC2           0x7d9
#define CSR_MSAVECAUSE2         0x7da
#define CSR_MSAVEDCAUSE1        0x7db
#define CSR_MSAVEDCAUSE2        0x7dc

/* Control CSRs */
#define CSR_MPFT_CTL            0x7c5
#define CSR_MMISC_CTL           0x7d0
#define CSR_MCLK_CTL            0x7df

/* Counter related CSRs */
#define CSR_MCOUNTERWEN         0x7ce
#define CSR_MCOUNTERINTEN       0x7cf
#define CSR_MCOUNTERMASK_M      0x7d1
#define CSR_MCOUNTERMASK_S      0x7d2
#define CSR_MCOUNTERMASK_U      0x7d3
#define CSR_MCOUNTEROVF         0x7d4

/* Enhanced CLIC CSRs */
#define CSR_MIRQ_ENTRY          0x7ec
#define CSR_MINTSEL_JAL         0x7ed
#define CSR_PUSHMCAUSE          0x7ee
#define CSR_PUSHMEPC            0x7ef
#define CSR_PUSHMXSTATUS        0x7eb

/* Andes Physical Memory Attribute(PMA) CSRs */
#define CSR_PMACFG0             0xbc0
#define CSR_PMACFG1             0xbc1
#define CSR_PMACFG2             0xbc2
#define CSR_PMACFG3             0xbc3
#define CSR_PMAADDR0            0xbd0
#define CSR_PMAADDR1            0xbd1
#define CSR_PMAADDR2            0xbd2
#define CSR_PMAADDR3            0xbd2
#define CSR_PMAADDR4            0xbd4
#define CSR_PMAADDR5            0xbd5
#define CSR_PMAADDR6            0xbd6
#define CSR_PMAADDR7            0xbd7
#define CSR_PMAADDR8            0xbd8
#define CSR_PMAADDR9            0xbd9
#define CSR_PMAADDR10           0xbda
#define CSR_PMAADDR11           0xbdb
#define CSR_PMAADDR12           0xbdc
#define CSR_PMAADDR13           0xbdd
#define CSR_PMAADDR14           0xbde
#define CSR_PMAADDR15           0xbdf

/* ========= AndeStar V5 supervisor mode CSRs ========= */
/* Supervisor trap registers */
#define CSR_SLIE                0x9c4
#define CSR_SLIP                0x9c5
#define CSR_SDCAUSE             0x9c9

/* Supervisor counter registers */
#define CSR_SCOUNTERINTEN       0x9cf
#define CSR_SCOUNTERMASK_M      0x9d1
#define CSR_SCOUNTERMASK_S      0x9d2
#define CSR_SCOUNTERMASK_U      0x9d3
#define CSR_SCOUNTEROVF         0x9d4
#define CSR_SCOUNTINHIBIT       0x9e0
#define CSR_SHPMEVENT3          0x9e3
#define CSR_SHPMEVENT4          0x9e4
#define CSR_SHPMEVENT5          0x9e5
#define CSR_SHPMEVENT6          0x9e6

/* Supervisor control registers */
#define CSR_SCCTLDATA           0x9cd
#define CSR_SMISC_CTL           0x9d0

#endif /* !defined(CONFIG_USER_ONLY) */

/* ========= AndeStar V5 user mode CSRs ========= */
/* User mode control registers */
#define CSR_UITB                0x800
#define CSR_UCODE               0x801
#define CSR_UDCAUSE             0x809
#define CSR_UCCTLBEGINADDR      0x80b
#define CSR_UCCTLCOMMAND        0x80c
#define CSR_WFE                 0x810
#define CSR_SLEEPVALUE          0x811
#define CSR_TXEVT               0x812
