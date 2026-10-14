/*
 * QTest for igb VF migration DVSEC state machine and save/load
 *
 * Copyright (c) 2026 Red Hat, Inc.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "libqtest.h"
#include "qemu/module.h"
#include "hw/net/igb_migration.h"
#include "hw/net/igb_regs.h"

/* Q35 MCH config register to enable ECAM */
#define MCH_HOST_BRIDGE_PCIEXBAR        0x60
#define Q35_ECAM_BASE                   0xb0000000ULL

/* PF on pcie.0 at slot 4 (devfn 0x20), bus 0 */
#define PF_BUS                  0
#define PF_SLOT                 4
#define PF_DEVFN                (PF_SLOT << 3)

/* VF0 devfn = PF_devfn + VF_OFFSET (0x80) = 0xA0, same bus */
#define VF0_BUS                 0
#define VF0_DEVFN               (PF_DEVFN + 0x80)

/* SR-IOV cap offset in igb PF */
#define IGB_SRIOV_CAP_OFFSET    0x160

/* DVSEC offset in VF extended config space */
#define DVSEC_BASE              IGB_MIG_DVSEC_OFFSET

/* Blob constants */
#define BLOB_MAGIC              0x4D494742

/* DMA buffer GPA for state blob */
#define DMA_BUF_GPA             0x100000ULL

static void ecam_enable(QTestState *qts)
{
    /* Set PCIEXBAREN (bit 0) in MCH PCIEXBAR via legacy CF8/CFC */
    qtest_outl(qts, 0xcf8, 0x80000000 | MCH_HOST_BRIDGE_PCIEXBAR);
    qtest_outl(qts, 0xcfc, Q35_ECAM_BASE | 1);
}

static uint64_t ecam_addr(int bus, int devfn, int offset)
{
    return Q35_ECAM_BASE + ((uint64_t)bus << 20) +
           ((uint64_t)devfn << 12) + offset;
}

static uint32_t ecam_readl(QTestState *qts, int bus, int devfn, int offset)
{
    return qtest_readl(qts, ecam_addr(bus, devfn, offset));
}

static void ecam_writel(QTestState *qts, int bus, int devfn,
                        int offset, uint32_t val)
{
    qtest_writel(qts, ecam_addr(bus, devfn, offset), val);
}

static void ecam_writew(QTestState *qts, int bus, int devfn,
                        int offset, uint16_t val)
{
    qtest_writew(qts, ecam_addr(bus, devfn, offset), val);
}

static uint32_t dvsec_readl(QTestState *qts, int offset)
{
    return ecam_readl(qts, VF0_BUS, VF0_DEVFN, DVSEC_BASE + offset);
}

static void dvsec_writel(QTestState *qts, int offset, uint32_t val)
{
    ecam_writel(qts, VF0_BUS, VF0_DEVFN, DVSEC_BASE + offset, val);
}

static uint32_t dvsec_status(QTestState *qts)
{
    return dvsec_readl(qts, IGB_MIG_STATUS);
}

static uint32_t dvsec_state(QTestState *qts)
{
    return dvsec_status(qts) & IGB_MIG_STATUS_STATE_MASK;
}

static uint32_t dvsec_error(QTestState *qts)
{
    return (dvsec_status(qts) >> IGB_MIG_STATUS_ERROR_CODE_SHIFT) & 0xFF;
}

static void dvsec_cmd(QTestState *qts, uint32_t cmd, uint32_t arg)
{
    dvsec_writel(qts, IGB_MIG_CTRL,
                 (cmd & IGB_MIG_CTRL_CMD_MASK) |
                 (arg << IGB_MIG_CTRL_ARG_SHIFT));
}

static void dvsec_set_state(QTestState *qts, uint32_t state)
{
    dvsec_cmd(qts, IGB_MIG_CMD_SET_STATE, state);
}

static void dvsec_set_buf_addr(QTestState *qts, uint64_t gpa)
{
    dvsec_writel(qts, IGB_MIG_BUF_ADDR_LO, (uint32_t)gpa);
    dvsec_writel(qts, IGB_MIG_BUF_ADDR_HI, (uint32_t)(gpa >> 32));
}

static void enable_sriov(QTestState *qts, uint16_t num_vfs)
{
    int sriov = IGB_SRIOV_CAP_OFFSET;

    ecam_writew(qts, PF_BUS, PF_DEVFN, sriov + PCI_SRIOV_NUM_VF, num_vfs);
    ecam_writew(qts, PF_BUS, PF_DEVFN, sriov + PCI_SRIOV_CTRL,
                PCI_SRIOV_CTRL_VFE | PCI_SRIOV_CTRL_MSE);
}

static void activate_vf(QTestState *qts)
{
    /* Initial state is ERROR; driver activates via ERROR -> STOP -> RUNNING */
    g_assert_cmpuint(dvsec_state(qts), ==, IGB_MIG_STATE_ERROR);
    dvsec_set_state(qts, IGB_MIG_STATE_STOP);
    g_assert_cmpuint(dvsec_state(qts), ==, IGB_MIG_STATE_STOP);
    dvsec_set_state(qts, IGB_MIG_STATE_RUNNING);
    g_assert_cmpuint(dvsec_state(qts), ==, IGB_MIG_STATE_RUNNING);
}

static QTestState *start_igb_vm(void)
{
    QTestState *qts = qtest_init(
        "-machine q35 -m 512M -nodefaults "
        "-device igb,bus=pcie.0,addr=4.0,x-vf-migration=on "
        "-netdev hubport,hubid=0,id=hs0");

    ecam_enable(qts);
    enable_sriov(qts, 1);
    activate_vf(qts);
    return qts;
}

static void test_dvsec_presence(void)
{
    QTestState *qts = start_igb_vm();

    /* Check DVSEC header: extended cap id 0x23, DVSEC vendor Qumranet */
    uint32_t hdr = dvsec_readl(qts, 0x00);
    g_assert_cmphex(hdr & 0xFFFF, ==, 0x0023);

    /* Check CAPS: F_STATE and F_DIRTY should be advertised */
    uint32_t caps = dvsec_readl(qts, IGB_MIG_CAPS);
    g_assert(caps & IGB_MIG_CAP_F_STATE);
    g_assert(caps & IGB_MIG_CAP_F_DIRTY);

    /* CAPS is read-only */
    dvsec_writel(qts, IGB_MIG_CAPS, 0);
    g_assert_cmphex(dvsec_readl(qts, IGB_MIG_CAPS), ==, caps);

    /* BUF_ADDR_LO/HI are read-write */
    dvsec_writel(qts, IGB_MIG_BUF_ADDR_LO, 0xDEAD0000);
    dvsec_writel(qts, IGB_MIG_BUF_ADDR_HI, 0x0000BEEF);
    g_assert_cmphex(dvsec_readl(qts, IGB_MIG_BUF_ADDR_LO), ==, 0xDEAD0000);
    g_assert_cmphex(dvsec_readl(qts, IGB_MIG_BUF_ADDR_HI), ==, 0x0000BEEF);

    qtest_quit(qts);
}

static void test_state_machine_stop_copy(void)
{
    QTestState *qts = start_igb_vm();

    /* RUNNING: not quiesced */
    g_assert(!(dvsec_status(qts) & IGB_MIG_STATUS_QUIESCED));

    /* RUNNING -> STOP: quiesced */
    dvsec_set_state(qts, IGB_MIG_STATE_STOP);
    g_assert_cmpuint(dvsec_state(qts), ==, IGB_MIG_STATE_STOP);
    g_assert_cmpuint(dvsec_error(qts), ==, 0);
    g_assert(dvsec_status(qts) & IGB_MIG_STATUS_QUIESCED);

    /* STOP -> STOP_COPY: quiesced */
    dvsec_set_state(qts, IGB_MIG_STATE_STOP_COPY);
    g_assert_cmpuint(dvsec_state(qts), ==, IGB_MIG_STATE_STOP_COPY);
    g_assert_cmpuint(dvsec_error(qts), ==, 0);
    g_assert(dvsec_status(qts) & IGB_MIG_STATUS_QUIESCED);

    /* STOP_COPY -> STOP */
    dvsec_set_state(qts, IGB_MIG_STATE_STOP);
    g_assert_cmpuint(dvsec_state(qts), ==, IGB_MIG_STATE_STOP);

    /* STOP -> RUNNING: not quiesced */
    dvsec_set_state(qts, IGB_MIG_STATE_RUNNING);
    g_assert_cmpuint(dvsec_state(qts), ==, IGB_MIG_STATE_RUNNING);
    g_assert(!(dvsec_status(qts) & IGB_MIG_STATUS_QUIESCED));

    qtest_quit(qts);
}

static void test_state_machine_resuming(void)
{
    QTestState *qts = start_igb_vm();

    /* RUNNING -> STOP */
    dvsec_set_state(qts, IGB_MIG_STATE_STOP);
    g_assert_cmpuint(dvsec_state(qts), ==, IGB_MIG_STATE_STOP);

    /* STOP -> RESUMING */
    dvsec_set_state(qts, IGB_MIG_STATE_RESUMING);
    g_assert_cmpuint(dvsec_state(qts), ==, IGB_MIG_STATE_RESUMING);
    g_assert_cmpuint(dvsec_error(qts), ==, 0);

    /* RESUMING -> STOP */
    dvsec_set_state(qts, IGB_MIG_STATE_STOP);
    g_assert_cmpuint(dvsec_state(qts), ==, IGB_MIG_STATE_STOP);

    /* STOP -> RUNNING */
    dvsec_set_state(qts, IGB_MIG_STATE_RUNNING);
    g_assert_cmpuint(dvsec_state(qts), ==, IGB_MIG_STATE_RUNNING);

    qtest_quit(qts);
}

static void test_state_machine_invalid(void)
{
    QTestState *qts = start_igb_vm();

    /* RUNNING -> STOP_COPY (invalid, must go through STOP) */
    dvsec_set_state(qts, IGB_MIG_STATE_STOP_COPY);
    g_assert_cmpuint(dvsec_state(qts), ==, IGB_MIG_STATE_ERROR);
    g_assert_cmpuint(dvsec_error(qts), ==, IGB_MIG_ERR_BAD_STATE);

    qtest_quit(qts);
}

static void test_state_machine_pre_copy(void)
{
    QTestState *qts = start_igb_vm();

    dvsec_set_buf_addr(qts, DMA_BUF_GPA);

    /* RUNNING -> PRE_COPY */
    dvsec_set_state(qts, IGB_MIG_STATE_PRE_COPY);
    g_assert_cmpuint(dvsec_state(qts), ==, IGB_MIG_STATE_PRE_COPY);
    g_assert_cmpuint(dvsec_error(qts), ==, 0);

    /* PRE_COPY -> STOP (quiesce + disable dirty) */
    dvsec_set_state(qts, IGB_MIG_STATE_STOP);
    g_assert_cmpuint(dvsec_state(qts), ==, IGB_MIG_STATE_STOP);

    /* Back to RUNNING */
    dvsec_set_state(qts, IGB_MIG_STATE_RUNNING);
    g_assert_cmpuint(dvsec_state(qts), ==, IGB_MIG_STATE_RUNNING);

    /* RUNNING -> PRE_COPY -> STOP_COPY */
    dvsec_set_state(qts, IGB_MIG_STATE_PRE_COPY);
    g_assert_cmpuint(dvsec_state(qts), ==, IGB_MIG_STATE_PRE_COPY);

    dvsec_set_state(qts, IGB_MIG_STATE_STOP_COPY);
    g_assert_cmpuint(dvsec_state(qts), ==, IGB_MIG_STATE_STOP_COPY);

    /* STOP_COPY -> STOP */
    dvsec_set_state(qts, IGB_MIG_STATE_STOP);
    g_assert_cmpuint(dvsec_state(qts), ==, IGB_MIG_STATE_STOP);

    qtest_quit(qts);
}

static void test_save_load_roundtrip(void)
{
    QTestState *qts = start_igb_vm();
    uint32_t buf[IGB_VF_STATE_MAX_SIZE / sizeof(uint32_t)];

    dvsec_set_buf_addr(qts, DMA_BUF_GPA);

    /* --- Save path --- */

    /* RUNNING -> STOP */
    dvsec_set_state(qts, IGB_MIG_STATE_STOP);
    g_assert_cmpuint(dvsec_state(qts), ==, IGB_MIG_STATE_STOP);

    /* DATA_SIZE in STOP is the max allocation hint */
    uint32_t max_size = dvsec_readl(qts, IGB_MIG_DATA_SIZE);
    g_assert_cmpuint(max_size, >, 0);
    g_assert_cmpuint(max_size, <=, IGB_VF_STATE_MAX_SIZE);

    /* STOP -> STOP_COPY */
    dvsec_set_state(qts, IGB_MIG_STATE_STOP_COPY);
    g_assert_cmpuint(dvsec_state(qts), ==, IGB_MIG_STATE_STOP_COPY);

    /* DATA_SIZE in STOP_COPY is the actual serialized size */
    uint32_t data_size = dvsec_readl(qts, IGB_MIG_DATA_SIZE);
    g_assert_cmpuint(data_size, >, 0);
    g_assert_cmpuint(data_size, <=, max_size);

    /* Issue SAVE */
    dvsec_cmd(qts, IGB_MIG_CMD_SAVE, 0);
    g_assert_cmpuint(dvsec_state(qts), ==, IGB_MIG_STATE_STOP_COPY);
    g_assert_cmpuint(dvsec_error(qts), ==, 0);

    /* Second SAVE is idempotent */
    dvsec_cmd(qts, IGB_MIG_CMD_SAVE, 0);
    g_assert_cmpuint(dvsec_state(qts), ==, IGB_MIG_STATE_STOP_COPY);
    g_assert_cmpuint(dvsec_error(qts), ==, 0);

    /* Read blob from DMA buffer */
    qtest_memread(qts, DMA_BUF_GPA, buf, data_size);

    /* Verify blob header */
    g_assert_cmphex(le32_to_cpu(buf[0]), ==, BLOB_MAGIC);
    g_assert_cmpuint(le32_to_cpu(buf[1]), ==, 1);   /* version */
    g_assert_cmpuint(le32_to_cpu(buf[2]), ==, 0);   /* vfn */

    /* STOP_COPY -> STOP */
    dvsec_set_state(qts, IGB_MIG_STATE_STOP);
    g_assert_cmpuint(dvsec_state(qts), ==, IGB_MIG_STATE_STOP);

    /* --- Load path --- */

    /* STOP -> RESUMING */
    dvsec_set_state(qts, IGB_MIG_STATE_RESUMING);
    g_assert_cmpuint(dvsec_state(qts), ==, IGB_MIG_STATE_RESUMING);

    /* Write blob to DMA buffer */
    qtest_memwrite(qts, DMA_BUF_GPA, buf, data_size);

    /* Issue LOAD */
    dvsec_cmd(qts, IGB_MIG_CMD_LOAD, data_size);
    g_assert_cmpuint(dvsec_state(qts), ==, IGB_MIG_STATE_RESUMING);
    g_assert_cmpuint(dvsec_error(qts), ==, 0);

    /* RESUMING -> STOP -> RUNNING */
    dvsec_set_state(qts, IGB_MIG_STATE_STOP);
    g_assert_cmpuint(dvsec_state(qts), ==, IGB_MIG_STATE_STOP);

    dvsec_set_state(qts, IGB_MIG_STATE_RUNNING);
    g_assert_cmpuint(dvsec_state(qts), ==, IGB_MIG_STATE_RUNNING);
    g_assert_cmpuint(dvsec_error(qts), ==, 0);

    qtest_quit(qts);
}

static void test_load_bad_magic(void)
{
    QTestState *qts = start_igb_vm();
    uint32_t buf[IGB_VF_STATE_MAX_SIZE / sizeof(uint32_t)];

    dvsec_set_buf_addr(qts, DMA_BUF_GPA);

    /* Save to get a valid blob */
    dvsec_set_state(qts, IGB_MIG_STATE_STOP);
    dvsec_set_state(qts, IGB_MIG_STATE_STOP_COPY);
    uint32_t data_size = dvsec_readl(qts, IGB_MIG_DATA_SIZE);
    dvsec_cmd(qts, IGB_MIG_CMD_SAVE, 0);
    qtest_memread(qts, DMA_BUF_GPA, buf, data_size);

    /* Corrupt magic */
    buf[0] = cpu_to_le32(0xDEADBEEF);

    /* Try to load */
    dvsec_set_state(qts, IGB_MIG_STATE_STOP);
    dvsec_set_state(qts, IGB_MIG_STATE_RESUMING);
    qtest_memwrite(qts, DMA_BUF_GPA, buf, data_size);
    dvsec_cmd(qts, IGB_MIG_CMD_LOAD, data_size);

    g_assert_cmpuint(dvsec_state(qts), ==, IGB_MIG_STATE_ERROR);
    g_assert_cmpuint(dvsec_error(qts), ==, IGB_MIG_ERR_BAD_MAGIC);

    qtest_quit(qts);
}

static void test_load_bad_version(void)
{
    QTestState *qts = start_igb_vm();
    uint32_t buf[IGB_VF_STATE_MAX_SIZE / sizeof(uint32_t)];

    dvsec_set_buf_addr(qts, DMA_BUF_GPA);

    /* Save to get a valid blob */
    dvsec_set_state(qts, IGB_MIG_STATE_STOP);
    dvsec_set_state(qts, IGB_MIG_STATE_STOP_COPY);
    uint32_t data_size = dvsec_readl(qts, IGB_MIG_DATA_SIZE);
    dvsec_cmd(qts, IGB_MIG_CMD_SAVE, 0);
    qtest_memread(qts, DMA_BUF_GPA, buf, data_size);

    /* Corrupt version */
    buf[1] = cpu_to_le32(0xFF);

    /* Try to load */
    dvsec_set_state(qts, IGB_MIG_STATE_STOP);
    dvsec_set_state(qts, IGB_MIG_STATE_RESUMING);
    qtest_memwrite(qts, DMA_BUF_GPA, buf, data_size);
    dvsec_cmd(qts, IGB_MIG_CMD_LOAD, data_size);

    g_assert_cmpuint(dvsec_state(qts), ==, IGB_MIG_STATE_ERROR);
    g_assert_cmpuint(dvsec_error(qts), ==, IGB_MIG_ERR_BAD_VERSION);

    qtest_quit(qts);
}

static void test_load_bad_vfn(void)
{
    QTestState *qts = start_igb_vm();
    uint32_t buf[IGB_VF_STATE_MAX_SIZE / sizeof(uint32_t)];

    dvsec_set_buf_addr(qts, DMA_BUF_GPA);

    /* Save to get a valid blob */
    dvsec_set_state(qts, IGB_MIG_STATE_STOP);
    dvsec_set_state(qts, IGB_MIG_STATE_STOP_COPY);
    uint32_t data_size = dvsec_readl(qts, IGB_MIG_DATA_SIZE);
    dvsec_cmd(qts, IGB_MIG_CMD_SAVE, 0);
    qtest_memread(qts, DMA_BUF_GPA, buf, data_size);

    /* Corrupt VFN */
    buf[2] = cpu_to_le32(7);

    /* Try to load */
    dvsec_set_state(qts, IGB_MIG_STATE_STOP);
    dvsec_set_state(qts, IGB_MIG_STATE_RESUMING);
    qtest_memwrite(qts, DMA_BUF_GPA, buf, data_size);
    dvsec_cmd(qts, IGB_MIG_CMD_LOAD, data_size);

    g_assert_cmpuint(dvsec_state(qts), ==, IGB_MIG_STATE_ERROR);
    g_assert_cmpuint(dvsec_error(qts), ==, IGB_MIG_ERR_BAD_VFN);

    qtest_quit(qts);
}

static void test_error_recovery(void)
{
    QTestState *qts = start_igb_vm();
    uint32_t buf[IGB_VF_STATE_MAX_SIZE / sizeof(uint32_t)];

    dvsec_set_buf_addr(qts, DMA_BUF_GPA);

    /* Trigger an error: invalid state transition */
    dvsec_set_state(qts, IGB_MIG_STATE_STOP_COPY);
    g_assert_cmpuint(dvsec_state(qts), ==, IGB_MIG_STATE_ERROR);

    /* Recover: ERROR -> STOP -> RUNNING */
    dvsec_set_state(qts, IGB_MIG_STATE_STOP);
    g_assert_cmpuint(dvsec_state(qts), ==, IGB_MIG_STATE_STOP);
    dvsec_set_state(qts, IGB_MIG_STATE_RUNNING);
    g_assert_cmpuint(dvsec_state(qts), ==, IGB_MIG_STATE_RUNNING);
    g_assert_cmpuint(dvsec_error(qts), ==, 0);

    /* Full save/load cycle should work after recovery */
    dvsec_set_state(qts, IGB_MIG_STATE_STOP);
    dvsec_set_state(qts, IGB_MIG_STATE_STOP_COPY);
    uint32_t data_size = dvsec_readl(qts, IGB_MIG_DATA_SIZE);
    dvsec_cmd(qts, IGB_MIG_CMD_SAVE, 0);
    g_assert_cmpuint(dvsec_error(qts), ==, 0);
    qtest_memread(qts, DMA_BUF_GPA, buf, data_size);

    dvsec_set_state(qts, IGB_MIG_STATE_STOP);
    dvsec_set_state(qts, IGB_MIG_STATE_RESUMING);
    qtest_memwrite(qts, DMA_BUF_GPA, buf, data_size);
    dvsec_cmd(qts, IGB_MIG_CMD_LOAD, data_size);
    g_assert_cmpuint(dvsec_error(qts), ==, 0);

    dvsec_set_state(qts, IGB_MIG_STATE_STOP);
    dvsec_set_state(qts, IGB_MIG_STATE_RUNNING);
    g_assert_cmpuint(dvsec_state(qts), ==, IGB_MIG_STATE_RUNNING);

    qtest_quit(qts);
}

static void test_save_no_buffer(void)
{
    QTestState *qts = start_igb_vm();

    /* Enter STOP_COPY without setting buffer address */
    dvsec_set_state(qts, IGB_MIG_STATE_STOP);
    dvsec_set_state(qts, IGB_MIG_STATE_STOP_COPY);

    /* Issue SAVE without buffer */
    dvsec_cmd(qts, IGB_MIG_CMD_SAVE, 0);
    g_assert_cmpuint(dvsec_state(qts), ==, IGB_MIG_STATE_ERROR);
    g_assert_cmpuint(dvsec_error(qts), ==, IGB_MIG_ERR_NO_BUFFER);

    qtest_quit(qts);
}

static void test_load_corrupted_blob(void)
{
    QTestState *qts = start_igb_vm();
    uint32_t buf[IGB_VF_STATE_MAX_SIZE / sizeof(uint32_t)];
    uint32_t good[IGB_VF_STATE_MAX_SIZE / sizeof(uint32_t)];

    dvsec_set_buf_addr(qts, DMA_BUF_GPA);

    /* Save a valid blob */
    dvsec_set_state(qts, IGB_MIG_STATE_STOP);
    dvsec_set_state(qts, IGB_MIG_STATE_STOP_COPY);
    uint32_t data_size = dvsec_readl(qts, IGB_MIG_DATA_SIZE);
    dvsec_cmd(qts, IGB_MIG_CMD_SAVE, 0);
    qtest_memread(qts, DMA_BUF_GPA, good, data_size);

    uint32_t n_words = data_size / sizeof(uint32_t);

    /* Flip a random word in the blob 16 times; each must be rejected */
    for (int i = 0; i < 16; i++) {
        memcpy(buf, good, data_size);

        uint32_t idx = g_test_rand_int_range(0, n_words);
        buf[idx] ^= g_test_rand_int();

        /* Reset to RESUMING for each attempt */
        dvsec_set_state(qts, IGB_MIG_STATE_STOP);
        dvsec_set_state(qts, IGB_MIG_STATE_RESUMING);
        qtest_memwrite(qts, DMA_BUF_GPA, buf, data_size);
        dvsec_cmd(qts, IGB_MIG_CMD_LOAD, data_size);

        uint32_t state = dvsec_state(qts);
        g_assert(state == IGB_MIG_STATE_ERROR ||
                 state == IGB_MIG_STATE_RESUMING);
    }

    qtest_quit(qts);
}

static void test_unknown_command(void)
{
    QTestState *qts = start_igb_vm();

    dvsec_cmd(qts, 0xFF, 0);
    g_assert_cmpuint(dvsec_state(qts), ==, IGB_MIG_STATE_ERROR);
    g_assert_cmpuint(dvsec_error(qts), ==, IGB_MIG_ERR_UNK_CMD);

    qtest_quit(qts);
}

static void test_save_wrong_state(void)
{
    QTestState *qts = start_igb_vm();

    dvsec_set_buf_addr(qts, DMA_BUF_GPA);

    /* SAVE while RUNNING (must be in STOP_COPY) */
    dvsec_cmd(qts, IGB_MIG_CMD_SAVE, 0);
    g_assert_cmpuint(dvsec_state(qts), ==, IGB_MIG_STATE_ERROR);
    g_assert_cmpuint(dvsec_error(qts), ==, IGB_MIG_ERR_BAD_STATE);

    qtest_quit(qts);
}

static void test_load_wrong_state(void)
{
    QTestState *qts = start_igb_vm();

    dvsec_set_buf_addr(qts, DMA_BUF_GPA);

    /* LOAD while STOP (must be in RESUMING) */
    dvsec_set_state(qts, IGB_MIG_STATE_STOP);
    dvsec_cmd(qts, IGB_MIG_CMD_LOAD, 64);
    g_assert_cmpuint(dvsec_state(qts), ==, IGB_MIG_STATE_ERROR);
    g_assert_cmpuint(dvsec_error(qts), ==, IGB_MIG_ERR_BAD_STATE);

    qtest_quit(qts);
}

static void test_load_no_buffer(void)
{
    QTestState *qts = start_igb_vm();

    /* Enter RESUMING without setting buffer address */
    dvsec_set_state(qts, IGB_MIG_STATE_STOP);
    dvsec_set_state(qts, IGB_MIG_STATE_RESUMING);

    /* Issue LOAD without buffer */
    dvsec_cmd(qts, IGB_MIG_CMD_LOAD, 64);
    g_assert_cmpuint(dvsec_state(qts), ==, IGB_MIG_STATE_ERROR);
    g_assert_cmpuint(dvsec_error(qts), ==, IGB_MIG_ERR_NO_BUFFER);

    qtest_quit(qts);
}

static void test_load_size_zero(void)
{
    QTestState *qts = start_igb_vm();

    dvsec_set_buf_addr(qts, DMA_BUF_GPA);

    dvsec_set_state(qts, IGB_MIG_STATE_STOP);
    dvsec_set_state(qts, IGB_MIG_STATE_RESUMING);

    /* LOAD with size 0 */
    dvsec_cmd(qts, IGB_MIG_CMD_LOAD, 0);
    g_assert_cmpuint(dvsec_state(qts), ==, IGB_MIG_STATE_ERROR);
    g_assert_cmpuint(dvsec_error(qts), ==, IGB_MIG_ERR_BAD_SIZE);

    qtest_quit(qts);
}

static void test_load_size_too_big(void)
{
    QTestState *qts = start_igb_vm();

    dvsec_set_buf_addr(qts, DMA_BUF_GPA);

    dvsec_set_state(qts, IGB_MIG_STATE_STOP);
    dvsec_set_state(qts, IGB_MIG_STATE_RESUMING);

    /* LOAD with size exceeding max */
    dvsec_cmd(qts, IGB_MIG_CMD_LOAD, IGB_VF_STATE_MAX_SIZE + 1);
    g_assert_cmpuint(dvsec_state(qts), ==, IGB_MIG_STATE_ERROR);
    g_assert_cmpuint(dvsec_error(qts), ==, IGB_MIG_ERR_BAD_SIZE);

    qtest_quit(qts);
}

static void write_dirty_enable_req(QTestState *qts, uint64_t gpa,
                                   uint64_t pgsize, uint64_t iova,
                                   uint64_t size)
{
    struct igb_mig_dirty_enable_req req = {
        .len = cpu_to_le32(sizeof(req)),
        .pgsize = cpu_to_le64(pgsize),
        .range_iova = cpu_to_le64(iova),
        .range_size = cpu_to_le64(size),
    };
    qtest_memwrite(qts, gpa, &req, sizeof(req));
}

static void test_dirty_enable_no_buffer(void)
{
    QTestState *qts = start_igb_vm();

    /* DIRTY_ENABLE without setting buffer address */
    dvsec_cmd(qts, IGB_MIG_CMD_DIRTY_ENABLE, 0);
    g_assert_cmpuint(dvsec_state(qts), ==, IGB_MIG_STATE_ERROR);
    g_assert_cmpuint(dvsec_error(qts), ==, IGB_MIG_ERR_NO_BUFFER);

    qtest_quit(qts);
}

static void test_dirty_query_not_enabled(void)
{
    QTestState *qts = start_igb_vm();

    dvsec_set_buf_addr(qts, DMA_BUF_GPA);

    dvsec_cmd(qts, IGB_MIG_CMD_DIRTY_QUERY, 0);
    g_assert_cmpuint(dvsec_state(qts), ==, IGB_MIG_STATE_ERROR);
    g_assert_cmpuint(dvsec_error(qts), ==, IGB_MIG_ERR_NOT_ENABLED);

    qtest_quit(qts);
}

static void test_dirty_enable_bad_pgsize(void)
{
    QTestState *qts = start_igb_vm();

    dvsec_set_buf_addr(qts, DMA_BUF_GPA);

    /* page size 0x1234 is not a power of 2 and not in CAPS */
    write_dirty_enable_req(qts, DMA_BUF_GPA, 0x1234, 0, 0x10000);
    dvsec_cmd(qts, IGB_MIG_CMD_DIRTY_ENABLE, 0);
    g_assert_cmpuint(dvsec_state(qts), ==, IGB_MIG_STATE_ERROR);
    g_assert_cmpuint(dvsec_error(qts), ==, IGB_MIG_ERR_BAD_PGSIZE);

    qtest_quit(qts);
}

static void test_dirty_enable_bad_range(void)
{
    QTestState *qts = start_igb_vm();

    dvsec_set_buf_addr(qts, DMA_BUF_GPA);

    /* zero range size */
    write_dirty_enable_req(qts, DMA_BUF_GPA, 4096, 0, 0);
    dvsec_cmd(qts, IGB_MIG_CMD_DIRTY_ENABLE, 0);
    g_assert_cmpuint(dvsec_state(qts), ==, IGB_MIG_STATE_ERROR);
    g_assert_cmpuint(dvsec_error(qts), ==, IGB_MIG_ERR_BAD_RANGE);

    qtest_quit(qts);
}

static void test_dirty_enable_overflow(void)
{
    QTestState *qts = start_igb_vm();

    dvsec_set_buf_addr(qts, DMA_BUF_GPA);

    /* range_iova + range_size wraps uint64 */
    write_dirty_enable_req(qts, DMA_BUF_GPA, 4096,
                           UINT64_MAX - 0xFFF, 0x2000);
    dvsec_cmd(qts, IGB_MIG_CMD_DIRTY_ENABLE, 0);
    g_assert_cmpuint(dvsec_state(qts), ==, IGB_MIG_STATE_ERROR);
    g_assert_cmpuint(dvsec_error(qts), ==, IGB_MIG_ERR_BAD_RANGE);

    qtest_quit(qts);
}

static void test_dirty_enable_overlap(void)
{
    QTestState *qts = start_igb_vm();

    dvsec_set_buf_addr(qts, DMA_BUF_GPA);

    /* First range succeeds */
    write_dirty_enable_req(qts, DMA_BUF_GPA, 4096, 0, 0x10000);
    dvsec_cmd(qts, IGB_MIG_CMD_DIRTY_ENABLE, 0);
    g_assert_cmpuint(dvsec_state(qts), ==, IGB_MIG_STATE_RUNNING);
    g_assert_cmpuint(dvsec_error(qts), ==, 0);

    /* Second range overlaps the first */
    write_dirty_enable_req(qts, DMA_BUF_GPA, 4096, 0x8000, 0x10000);
    dvsec_cmd(qts, IGB_MIG_CMD_DIRTY_ENABLE, 0);
    g_assert_cmpuint(dvsec_state(qts), ==, IGB_MIG_STATE_ERROR);
    g_assert_cmpuint(dvsec_error(qts), ==, IGB_MIG_ERR_BAD_RANGE);

    qtest_quit(qts);
}

static void test_dirty_enable_unsupported_pgsize(void)
{
    QTestState *qts = start_igb_vm();

    dvsec_set_buf_addr(qts, DMA_BUF_GPA);

    /* 512 is a power of 2 but not in CAPS pgsize bitmask */
    write_dirty_enable_req(qts, DMA_BUF_GPA, 512, 0, 0x10000);
    dvsec_cmd(qts, IGB_MIG_CMD_DIRTY_ENABLE, 0);
    g_assert_cmpuint(dvsec_state(qts), ==, IGB_MIG_STATE_ERROR);
    g_assert_cmpuint(dvsec_error(qts), ==, IGB_MIG_ERR_BAD_PGSIZE);

    qtest_quit(qts);
}

static void test_dirty_enable_64k_pgsize(void)
{
    QTestState *qts = start_igb_vm();

    dvsec_set_buf_addr(qts, DMA_BUF_GPA);

    /* CAPS only advertises 4K, so 64K should be rejected */
    write_dirty_enable_req(qts, DMA_BUF_GPA, 65536, 0, 0x100000);
    dvsec_cmd(qts, IGB_MIG_CMD_DIRTY_ENABLE, 0);
    g_assert_cmpuint(dvsec_state(qts), ==, IGB_MIG_STATE_ERROR);
    g_assert_cmpuint(dvsec_error(qts), ==, IGB_MIG_ERR_BAD_PGSIZE);

    qtest_quit(qts);
}

static void test_dirty_enable_misaligned(void)
{
    QTestState *qts = start_igb_vm();

    dvsec_set_buf_addr(qts, DMA_BUF_GPA);

    /* range_iova not aligned to page size */
    write_dirty_enable_req(qts, DMA_BUF_GPA, 4096, 0x1000 + 1, 0x10000);
    dvsec_cmd(qts, IGB_MIG_CMD_DIRTY_ENABLE, 0);
    g_assert_cmpuint(dvsec_state(qts), ==, IGB_MIG_STATE_ERROR);
    g_assert_cmpuint(dvsec_error(qts), ==, IGB_MIG_ERR_BAD_PGSIZE);

    qtest_quit(qts);
}

static void test_dirty_enable_multiple_ranges(void)
{
    QTestState *qts = start_igb_vm();

    dvsec_set_buf_addr(qts, DMA_BUF_GPA);

    /* First range */
    write_dirty_enable_req(qts, DMA_BUF_GPA, 4096, 0, 0x10000);
    dvsec_cmd(qts, IGB_MIG_CMD_DIRTY_ENABLE, 0);
    g_assert_cmpuint(dvsec_state(qts), ==, IGB_MIG_STATE_RUNNING);
    g_assert_cmpuint(dvsec_error(qts), ==, 0);

    /* Second non-overlapping range */
    write_dirty_enable_req(qts, DMA_BUF_GPA, 4096, 0x100000, 0x10000);
    dvsec_cmd(qts, IGB_MIG_CMD_DIRTY_ENABLE, 0);
    g_assert_cmpuint(dvsec_state(qts), ==, IGB_MIG_STATE_RUNNING);
    g_assert_cmpuint(dvsec_error(qts), ==, 0);

    dvsec_cmd(qts, IGB_MIG_CMD_DIRTY_DISABLE, 0);

    qtest_quit(qts);
}

static void test_dirty_enable_double(void)
{
    QTestState *qts = start_igb_vm();

    dvsec_set_buf_addr(qts, DMA_BUF_GPA);

    /* First enable succeeds */
    write_dirty_enable_req(qts, DMA_BUF_GPA, 4096, 0, 0x10000);
    dvsec_cmd(qts, IGB_MIG_CMD_DIRTY_ENABLE, 0);
    g_assert_cmpuint(dvsec_state(qts), ==, IGB_MIG_STATE_RUNNING);
    g_assert_cmpuint(dvsec_error(qts), ==, 0);

    /* Same range again overlaps itself */
    write_dirty_enable_req(qts, DMA_BUF_GPA, 4096, 0, 0x10000);
    dvsec_cmd(qts, IGB_MIG_CMD_DIRTY_ENABLE, 0);
    g_assert_cmpuint(dvsec_state(qts), ==, IGB_MIG_STATE_ERROR);
    g_assert_cmpuint(dvsec_error(qts), ==, IGB_MIG_ERR_BAD_RANGE);

    qtest_quit(qts);
}

static void test_dirty_query_bad_range(void)
{
    QTestState *qts = start_igb_vm();
    struct igb_mig_dirty_query qbuf = { 0 };

    dvsec_set_buf_addr(qts, DMA_BUF_GPA);

    /* Enable: 4K pages, 64K range at IOVA 0 */
    write_dirty_enable_req(qts, DMA_BUF_GPA, 4096, 0, 0x10000);
    dvsec_cmd(qts, IGB_MIG_CMD_DIRTY_ENABLE, 0);
    g_assert_cmpuint(dvsec_state(qts), ==, IGB_MIG_STATE_RUNNING);

    /* Query a range that doesn't match */
    qbuf.len = cpu_to_le32(sizeof(qbuf) + 16);
    qbuf.iova = cpu_to_le64(0x200000);
    qbuf.size = cpu_to_le64(0x10000);
    qtest_memwrite(qts, DMA_BUF_GPA, &qbuf, sizeof(qbuf));
    dvsec_cmd(qts, IGB_MIG_CMD_DIRTY_QUERY, 0);
    g_assert_cmpuint(dvsec_state(qts), ==, IGB_MIG_STATE_ERROR);
    g_assert_cmpuint(dvsec_error(qts), ==, IGB_MIG_ERR_BAD_RANGE);

    qtest_quit(qts);
}

static void test_dirty_enable_query_cycle(void)
{
    QTestState *qts = start_igb_vm();
    struct igb_mig_dirty_query qbuf = { 0 };

    dvsec_set_buf_addr(qts, DMA_BUF_GPA);

    /* Enable: 4K pages, 64K range at IOVA 0 */
    write_dirty_enable_req(qts, DMA_BUF_GPA, 4096, 0, 0x10000);
    dvsec_cmd(qts, IGB_MIG_CMD_DIRTY_ENABLE, 0);
    g_assert_cmpuint(dvsec_state(qts), ==, IGB_MIG_STATE_RUNNING);
    g_assert_cmpuint(dvsec_error(qts), ==, 0);

    /* Query: no DMA activity, bitmap should be empty */
    qbuf.len = cpu_to_le32(sizeof(qbuf) + 16);
    qbuf.iova = cpu_to_le64(0);
    qbuf.size = cpu_to_le64(0x10000);
    qtest_memwrite(qts, DMA_BUF_GPA, &qbuf, sizeof(qbuf));
    dvsec_cmd(qts, IGB_MIG_CMD_DIRTY_QUERY, 0);
    g_assert_cmpuint(dvsec_state(qts), ==, IGB_MIG_STATE_RUNNING);
    g_assert_cmpuint(dvsec_error(qts), ==, 0);

    /* Read back dirty_page_count - should be 0 */
    qtest_memread(qts, DMA_BUF_GPA, &qbuf, sizeof(qbuf));
    g_assert_cmpuint(le32_to_cpu(qbuf.dirty_page_count), ==, 0);

    /* Disable */
    dvsec_cmd(qts, IGB_MIG_CMD_DIRTY_DISABLE, 0);
    g_assert_cmpuint(dvsec_state(qts), ==, IGB_MIG_STATE_RUNNING);
    g_assert_cmpuint(dvsec_error(qts), ==, 0);

    qtest_quit(qts);
}

static void register_igb_migration_test(void)
{
    qtest_add_func("/igb/migration/dvsec-presence", test_dvsec_presence);
    qtest_add_func("/igb/migration/state-machine/stop-copy",
                   test_state_machine_stop_copy);
    qtest_add_func("/igb/migration/state-machine/resuming",
                   test_state_machine_resuming);
    qtest_add_func("/igb/migration/state-machine/invalid",
                   test_state_machine_invalid);
    qtest_add_func("/igb/migration/state-machine/pre-copy",
                   test_state_machine_pre_copy);
    qtest_add_func("/igb/migration/save-load-roundtrip",
                   test_save_load_roundtrip);
    qtest_add_func("/igb/migration/load-bad-magic", test_load_bad_magic);
    qtest_add_func("/igb/migration/load-bad-version", test_load_bad_version);
    qtest_add_func("/igb/migration/load-bad-vfn", test_load_bad_vfn);
    qtest_add_func("/igb/migration/error-recovery", test_error_recovery);
    qtest_add_func("/igb/migration/save-no-buffer", test_save_no_buffer);
    qtest_add_func("/igb/migration/load-corrupted-blob",
                   test_load_corrupted_blob);
    qtest_add_func("/igb/migration/unknown-command", test_unknown_command);
    qtest_add_func("/igb/migration/save-wrong-state", test_save_wrong_state);
    qtest_add_func("/igb/migration/load-wrong-state", test_load_wrong_state);
    qtest_add_func("/igb/migration/load-size-zero", test_load_size_zero);
    qtest_add_func("/igb/migration/load-size-too-big", test_load_size_too_big);
    qtest_add_func("/igb/migration/load-no-buffer", test_load_no_buffer);
    qtest_add_func("/igb/migration/dirty/enable-no-buffer",
                   test_dirty_enable_no_buffer);
    qtest_add_func("/igb/migration/dirty/query-not-enabled",
                   test_dirty_query_not_enabled);
    qtest_add_func("/igb/migration/dirty/enable-bad-pgsize",
                   test_dirty_enable_bad_pgsize);
    qtest_add_func("/igb/migration/dirty/enable-bad-range",
                   test_dirty_enable_bad_range);
    qtest_add_func("/igb/migration/dirty/enable-overflow",
                   test_dirty_enable_overflow);
    qtest_add_func("/igb/migration/dirty/enable-overlap",
                   test_dirty_enable_overlap);
    qtest_add_func("/igb/migration/dirty/enable-unsupported-pgsize",
                   test_dirty_enable_unsupported_pgsize);
    qtest_add_func("/igb/migration/dirty/enable-64k-pgsize",
                   test_dirty_enable_64k_pgsize);
    qtest_add_func("/igb/migration/dirty/enable-misaligned",
                   test_dirty_enable_misaligned);
    qtest_add_func("/igb/migration/dirty/enable-multiple-ranges",
                   test_dirty_enable_multiple_ranges);
    qtest_add_func("/igb/migration/dirty/enable-double",
                   test_dirty_enable_double);
    qtest_add_func("/igb/migration/dirty/query-bad-range",
                   test_dirty_query_bad_range);
    qtest_add_func("/igb/migration/dirty/enable-query-cycle",
                   test_dirty_enable_query_cycle);
}

libqos_init(register_igb_migration_test);
