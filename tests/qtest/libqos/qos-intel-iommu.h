/*
 * QOS Intel IOMMU (VT-d) Module
 *
 * This module provides Intel IOMMU-specific helper functions for libqos tests,
 * encapsulating VT-d setup, assertion, and cleanup operations.
 *
 * Copyright (c) 2026 Fengyuan Yu <15fengyuan@gmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef QTEST_LIBQOS_INTEL_IOMMU_H
#define QTEST_LIBQOS_INTEL_IOMMU_H

#include "hw/misc/iommu-testdev.h"
#include "hw/i386/intel_iommu_internal.h"

/*
 * Intel IOMMU MMIO register base. This is the standard Q35 IOMMU address.
 */
#define Q35_IOMMU_BASE            0xfed90000ULL

/*
 * Guest memory layout for IOMMU structures.
 * All structures are placed in guest physical memory inside the 512MB RAM.
 * Using 256MB mark (0x10000000) as base to ensure all structures fit in RAM.
 */
#define QVTD_MEM_BASE             0x10000000ULL

/* Root Entry Table: 256 entries * 16 bytes = 4KB */
#define QVTD_ROOT_TABLE_BASE      (QVTD_MEM_BASE + 0x00000000)

/* Context Entry Table: 256 entries * 16 bytes = 4KB per bus */
#define QVTD_CONTEXT_TABLE_BASE   (QVTD_MEM_BASE + 0x00001000)

/* Page Tables: 4-level hierarchy for 48-bit address translation */
#define QVTD_PT_L4_BASE           (QVTD_MEM_BASE + 0x00010000)  /* PML4 */
#define QVTD_PT_L3_BASE           (QVTD_MEM_BASE + 0x00011000)  /* PDPT */
#define QVTD_PT_L2_BASE           (QVTD_MEM_BASE + 0x00012000)  /* PD */
#define QVTD_PT_L1_BASE           (QVTD_MEM_BASE + 0x00013000)  /* PT */

/* Invalidation Queue: 256 entries * 16 bytes = 4KB */
#define QVTD_INV_QUEUE_BASE       (QVTD_MEM_BASE + 0x00020000)

/* Test IOVA and target physical address */
#define QVTD_TEST_IOVA            0x0000008080604000ULL
#define QVTD_TEST_PA              (QVTD_MEM_BASE + 0x00100000)

/*
 * Translation modes supported by Intel IOMMU
 */
typedef enum QVTDTransMode {
    QVTD_TM_LEGACY_PT,      /* Legacy pass-through mode */
    QVTD_TM_LEGACY_TRANS,   /* Legacy translated mode (4-level paging) */
} QVTDTransMode;

/*
 * Test configuration structure
 */
typedef struct QVTDTestConfig {
    QVTDTransMode trans_mode;     /* Translation mode */
    uint64_t dma_iova;            /* DMA IOVA address for testing */
    uint64_t dma_pa;              /* Target physical address */
    uint32_t dma_len;             /* DMA length for testing */
    uint32_t expected_result;     /* Expected DMA result */
    uint16_t domain_id;           /* Domain ID for this test */
} QVTDTestConfig;

/*
 * Test context structure
 */
typedef struct QVTDTestContext {
    QTestState *qts;              /* QTest state handle */
    QPCIDevice *dev;              /* PCI device handle */
    QPCIBar bar;                  /* PCI BAR for MMIO access */
    QVTDTestConfig config;        /* Test configuration */
    uint64_t iommu_base;          /* Intel IOMMU base address */
    uint32_t trans_status;        /* Translation configuration status */
    uint32_t dma_result;          /* DMA operation result */
    uint16_t sid;                 /* Source ID (bus:devfn) */
} QVTDTestContext;

/*
 * qvtd_setup_and_enable_translation - Complete translation setup and enable
 *
 * @ctx: Test context containing configuration and device handles
 *
 * Returns: Translation status (0 = success, non-zero = error)
 *
 * This function performs the complete translation setup sequence:
 * 1. Builds all required VT-d structures (root entry, context entry, page tables)
 * 2. Programs IOMMU registers
 * 3. Invalidates caches
 * 4. Enables translation
 */
uint32_t qvtd_setup_and_enable_translation(QVTDTestContext *ctx);

/*
 * qvtd_build_translation - Build Intel IOMMU translation structures
 *
 * @qts: QTest state handle
 * @mode: Translation mode (pass-through or translated)
 * @sid: Source ID (bus:devfn)
 * @domain_id: Domain ID
 * @iova: IOVA address for logging purposes
 * @pa: Physical address backed by the mapping
 *
 * Returns: Build status (0 = success, non-zero = error)
 *
 * Constructs all necessary VT-d translation structures in guest memory:
 * - Root Entry for the device's bus
 * - Context Entry for the device
 * - Complete 4-level page table hierarchy (if translated mode)
 */
uint32_t qvtd_build_translation(QTestState *qts, QVTDTransMode mode,
                                uint16_t sid, uint16_t domain_id,
                                uint64_t iova, uint64_t pa);

/*
 * qvtd_program_regs - Program Intel IOMMU registers
 *
 * @qts: QTest state handle
 * @iommu_base: IOMMU base address
 *
 * Programs IOMMU registers with the following sequence:
 * 1. Disable translation
 * 2. Program root table address
 * 3. Set root table pointer
 * 4. Unmask fault event interrupt
 *
 * Note: This function does NOT clear memory regions or enable translation.
 * Memory clearing should be done once during test setup via qvtd_clear_memory_regions().
 * Translation is enabled separately after building all structures.
 */
void qvtd_program_regs(QTestState *qts, uint64_t iommu_base);

/*
 * qvtd_trigger_dma - Trigger DMA operation via iommu-testdev
 *
 * @ctx: Test context
 *
 * Returns: DMA result code
 *
 * Programs iommu-testdev BAR0 registers to trigger a DMA operation:
 * 1. Write IOVA address (GVA_LO/HI)
 * 2. Write DMA length
 * 3. Arm DMA (write to DBELL)
 * 4. Trigger DMA (read from TRIGGERING)
 * 5. Poll for completion (read DMA_RESULT)
 */
uint32_t qvtd_trigger_dma(QVTDTestContext *ctx);

/*
 * qvtd_cleanup_translation - Clean up translation configuration
 *
 * @ctx: Test context containing configuration and device handles
 *
 * Clears all translation structures and invalidates IOMMU caches.
 */
void qvtd_cleanup_translation(QVTDTestContext *ctx);

/*
 * qvtd_validate_test_result - Validate actual vs expected test result
 *
 * @ctx: Test context containing actual and expected results
 *
 * Returns: true if test passed (actual == expected), false otherwise
 *
 * Compares the actual DMA result with the expected result and logs
 * the comparison for debugging purposes.
 */
bool qvtd_validate_test_result(QVTDTestContext *ctx);

/*
 * qvtd_setup_translation_tables - Setup complete VT-d page table hierarchy
 *
 * @qts: QTest state handle
 * @iova: Input Virtual Address to translate
 * @pa: Physical address to map to
 * @mode: Translation mode
 *
 * This function builds the complete 4-level page table structure for translating
 * the given IOVA to PA through Intel VT-d. The structure is:
 * - PML4 (Level 4): IOVA bits [47:39]
 * - PDPT (Level 3): IOVA bits [38:30]
 * - PD (Level 2): IOVA bits [29:21]
 * - PT (Level 1): IOVA bits [20:12]
 * - Page offset: IOVA bits [11:0]
 *
 * The function writes all necessary Page Table Entries (PTEs) to guest
 * memory using qtest_writeq(), setting up the complete translation path
 * that the VT-d hardware will traverse during DMA operations.
 */
void qvtd_setup_translation_tables(QTestState *qts, uint64_t iova,
                                   uint64_t pa, QVTDTransMode mode);

/*
 * qvtd_expected_dma_result - Calculate expected DMA result
 *
 * @ctx: Test context containing configuration
 *
 * Returns: Expected DMA result code
 *
 * This function acts as a test oracle, calculating the expected DMA result
 * based on the test configuration. It centralizes validation logic for
 * different scenarios (pass-through vs. translated, fault conditions).
 */
uint32_t qvtd_expected_dma_result(QVTDTestContext *ctx);

/*
 * qvtd_build_dma_attrs - Build DMA attributes for an Intel VT-d DMA request
 *
 * @bdf: PCI requester ID encoded as Bus[15:8]/Device[7:3]/Function[2:0]
 * @pasid: PASID tag (0 for legacy requests, non-zero for scalable mode)
 *
 * Returns: Value to program into iommu-testdev's DMA_ATTRS register
 *
 * The iommu-testdev attribute register mirrors Intel VT-d request metadata:
 *   - bits[2:0] keep the generic iommu-testdev fields (secure + space)
 *   - bit[4] selects legacy (0) vs. scalable (1) transactions
 *   - bits[23:8] carry the requester ID as defined in the VT-d spec
 *   - bits[31:24] carry the PASID (limited to 8 bits in QEMU, matching
 *     the MemTxAttrs::pid width and ECAP.PSS advertisement)
 *
 * The helper validates the BDF layout (bus <= 255, device <= 31, function <= 7)
 * and makes sure PASID fits in the supported width before returning the value.
 */
uint32_t qvtd_build_dma_attrs(uint16_t bdf, uint32_t pasid);

/*
 * High-level test execution functions
 */

/*
 * qvtd_single_translation - Execute single translation test
 *
 * @ctx: Test context
 *
 * Performs a complete test cycle:
 * 1. Setup translation structures
 * 2. Trigger DMA operation
 * 3. Validate results
 * 4. Cleanup
 */
void qvtd_single_translation(QVTDTestContext *ctx);

/*
 * qvtd_run_translation_case - Execute a single Intel VT-d translation test
 *
 * @qts: QTestState for the test
 * @dev: PCI device (iommu-testdev)
 * @bar: BAR0 of iommu-testdev
 * @iommu_base: Base address of Intel IOMMU MMIO registers
 * @cfg: Test configuration
 *
 * High-level wrapper that creates test context internally and executes
 * a single translation test case. This provides a simpler API compared
 * to qvtd_single_translation() which requires manual context initialization.
 *
 * This function is analogous to qriommu_run_translation_case() in the
 * RISC-V IOMMU test framework, providing a consistent API across different
 * IOMMU architectures.
 *
 * Example usage:
 *   QVTDTestConfig cfg = {
 *       .trans_mode = QVTD_TM_LEGACY_PT,
 *       .domain_id = 1,
 *       .dma_iova = 0x40100000,
 *       .dma_pa = 0x40100000,
 *       .dma_len = 4,
 *   };
 *   qvtd_run_translation_case(qts, dev, bar, iommu_base, &cfg);
 */
void qvtd_run_translation_case(QTestState *qts, QPCIDevice *dev,
                               QPCIBar bar, uint64_t iommu_base,
                               const QVTDTestConfig *cfg);

/*
 * qvtd_translation_batch - Execute batch of translation tests
 *
 * @configs: Array of test configurations
 * @count: Number of configurations
 * @qts: QTest state handle
 * @dev: PCI device handle
 * @bar: PCI BAR for MMIO access
 * @iommu_base: IOMMU base address
 *
 * Executes multiple translation tests in sequence, each with its own
 * configuration. Useful for testing different translation modes and
 * scenarios in a single test run.
 *
 * This function now uses qvtd_run_translation_case() internally to
 * reduce code duplication.
 */
void qvtd_translation_batch(const QVTDTestConfig *configs, size_t count,
                            QTestState *qts, QPCIDevice *dev,
                            QPCIBar bar, uint64_t iommu_base);

#endif /* QTEST_LIBQOS_INTEL_IOMMU_H */
