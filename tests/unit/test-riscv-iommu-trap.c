/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Deterministic tests of the private RISC-V IOMMU MSI trap callback. */
#include "qemu/osdep.h"
#include "qemu/bswap.h"
#include "hw/riscv/riscv-iommu.h"

#define TEST_IOVA       0x400040ull
#define TEST_GPA        0x28000040ull
#define TEST_DATA       0x1122334455667788ull

static struct {
    RISCVIOMMUState state;
    RISCVIOMMUContext ctx;
    unsigned ref_cookie;
    AddressSpace physical_as;
    AddressSpace changed_as;
    MemTxAttrs attrs;
    bool missing_ctx;
    int walk_fault;
    AddressSpace *walk_as;
    hwaddr walk_addr;
    MemTxResult msi_result;
    MemTxResult dma_result;
    unsigned size;
    unsigned ctx_calls;
    unsigned put_calls;
    unsigned walk_calls;
    unsigned msi_calls;
    unsigned fault_calls;
    unsigned dma_calls;
} test;

static void assert_attrs(MemTxAttrs attrs)
{
    g_assert_cmpuint(attrs.requester_id, ==, test.attrs.requester_id);
    g_assert_cmpuint(attrs.pid, ==, test.attrs.pid);
    g_assert_cmpuint(attrs.secure, ==, test.attrs.secure);
    g_assert_cmpuint(attrs.space, ==, test.attrs.space);
    g_assert_cmpuint(attrs.user, ==, test.attrs.user);
    g_assert_cmpuint(attrs.memory, ==, test.attrs.memory);
    g_assert_cmpuint(attrs.debug, ==, test.attrs.debug);
    g_assert_cmpuint(attrs.address_type, ==, test.attrs.address_type);
    g_assert_cmpuint(attrs.unspecified, ==, test.attrs.unspecified);
}

static RISCVIOMMUContext *test_ctx(RISCVIOMMUState *s, unsigned devid,
                                 unsigned pid, IOMMUAccessFlags perm,
                                 uint64_t iova, void **ref)
{
    g_assert_true(s == &test.state);
    g_assert_cmpuint(devid, ==,
                     test.attrs.requester_id | (test.state.bus << 8));
    g_assert_cmpuint(pid, ==, test.attrs.pid);
    g_assert_cmpuint(perm, ==, IOMMU_WO);
    g_assert_cmphex(iova, ==, TEST_IOVA);
    test.ctx_calls++;
    /* Cached contexts return a separate reference for ctx_put(). */
    *ref = test.missing_ctx ? NULL : &test.ref_cookie;
    return test.missing_ctx ? NULL : &test.ctx;
}

static void test_ctx_put(RISCVIOMMUState *s, void *ref)
{
    g_assert_true(s == &test.state);
    g_assert_true(ref == (test.missing_ctx ? NULL : &test.ref_cookie));
    test.put_calls++;
}

static int test_spa_fetch(RISCVIOMMUState *s, RISCVIOMMUContext *ctx,
                          IOMMUTLBEntry *iotlb, bool update_hpm)
{
    g_assert_true(s == &test.state);
    g_assert_true(ctx == &test.ctx);
    g_assert_cmphex(iotlb->iova, ==, TEST_IOVA);
    g_assert_true(iotlb->target_as == &test.physical_as);
    g_assert_cmphex(iotlb->addr_mask, ==, UINT64_MAX);
    g_assert_cmpuint(iotlb->perm, ==, IOMMU_WO);
    g_assert_false(update_hpm);
    test.walk_calls++;
    iotlb->target_as = test.walk_as;
    iotlb->translated_addr = test.walk_addr;
    return test.walk_fault;
}

static MemTxResult test_msi_write(RISCVIOMMUState *s,
                                  RISCVIOMMUContext *ctx, uint64_t iova,
                                  uint64_t gpa, uint64_t data, unsigned size,
                                  MemTxAttrs attrs)
{
    g_assert_true(s == &test.state);
    g_assert_true(ctx == &test.ctx);
    g_assert_cmphex(iova, ==, TEST_IOVA);
    g_assert_cmphex(gpa, ==, test.walk_addr);
    g_assert_cmphex(iova, !=, gpa);
    g_assert_cmphex(data, ==, TEST_DATA);
    g_assert_cmpuint(size, ==, test.size);
    assert_attrs(attrs);
    test.msi_calls++;
    return test.msi_result;
}

static void test_report_fault(RISCVIOMMUState *s, RISCVIOMMUContext *ctx,
                              uint32_t ttype, uint32_t cause, bool pv,
                              uint64_t iotval, uint64_t iotval2)
{
    g_assert_true(s == &test.state);
    g_assert_true(ctx == &test.ctx);
    g_assert_cmpuint(ttype, ==, RISCV_IOMMU_FQ_TTYPE_UADDR_WR);
    g_assert_cmpuint(cause, ==, test.walk_fault);
    /* Check the existing callback policy, not the complete PID/DPE model. */
    g_assert_cmpuint(pv, ==, !!(test.ctx.tc & RISCV_IOMMU_DC_TC_PDTV));
    g_assert_cmphex(iotval, ==, TEST_IOVA);
    g_assert_cmphex(iotval2, ==, test.walk_addr);
    test.fault_calls++;
}

static MemTxResult test_dma_write(AddressSpace *as, dma_addr_t addr,
                                  const void *buf, dma_addr_t len,
                                  MemTxAttrs attrs)
{
    static const uint8_t expected[] = {
        0x88, 0x77, 0x66, 0x55, 0x44, 0x33, 0x22, 0x11,
    };

    g_assert_true(as == &test.changed_as);
    g_assert_cmphex(addr, ==, test.walk_addr);
    g_assert_cmpuint(len, ==, test.size);
    g_assert_cmpmem(buf, len, expected, len);
    assert_attrs(attrs);
    test.dma_calls++;
    return test.dma_result;
}

/* Redirect dependencies only while compiling the private implementation. */
#define riscv_iommu_ctx test_ctx
#define riscv_iommu_ctx_put test_ctx_put
#define riscv_iommu_spa_fetch test_spa_fetch
#define riscv_iommu_msi_write test_msi_write
#define riscv_iommu_report_fault test_report_fault
#define dma_memory_write test_dma_write
#include "hw/riscv/riscv-iommu-trap.c.inc"
#undef riscv_iommu_ctx
#undef riscv_iommu_ctx_put
#undef riscv_iommu_spa_fetch
#undef riscv_iommu_msi_write
#undef riscv_iommu_report_fault
#undef dma_memory_write

static void reset_test(void)
{
    memset(&test, 0, sizeof(test));
    test.state.target_as = &test.physical_as;
    test.state.bus = 0x80;
    test.attrs = (MemTxAttrs) {
        .requester_id = 0x3456,
        .pid = 0x5a,
        .secure = true,
        .space = 1,
        .user = true,
        .memory = true,
        .debug = true,
        .address_type = 1,
    };
    test.ctx.process_id = test.attrs.pid;
    test.walk_as = &test.state.trap_as;
    test.walk_addr = TEST_GPA;
    test.size = 4;
}

static MemTxResult trap_write(void)
{
    return riscv_iommu_trap_write(&test.state, TEST_IOVA, TEST_DATA,
                                  test.size, test.attrs);
}

static void assert_calls(unsigned ctx, unsigned walk, unsigned msi,
                         unsigned fault, unsigned dma)
{
    g_assert_cmpuint(test.ctx_calls, ==, ctx);
    g_assert_cmpuint(test.put_calls, ==, ctx);
    g_assert_cmpuint(test.walk_calls, ==, walk);
    g_assert_cmpuint(test.msi_calls, ==, msi);
    g_assert_cmpuint(test.fault_calls, ==, fault);
    g_assert_cmpuint(test.dma_calls, ==, dma);
}

static void test_unspecified(void)
{
    reset_test();
    test.attrs.unspecified = true;
    g_assert_cmpuint(trap_write(), ==, MEMTX_ACCESS_ERROR);
    assert_calls(0, 0, 0, 0, 0);
}

static void test_missing_context(void)
{
    reset_test();
    test.missing_ctx = true;
    g_assert_cmpuint(trap_write(), ==, MEMTX_ACCESS_ERROR);
    assert_calls(1, 0, 0, 0, 0);
}

static void test_msi_forward(void)
{
    reset_test();
    test.size = 8;
    test.msi_result = MEMTX_ERROR;
    g_assert_cmpuint(trap_write(), ==, MEMTX_ERROR);
    assert_calls(1, 1, 1, 0, 0);
}

static void test_rewalk_fault(gconstpointer data)
{
    reset_test();
    test.attrs.pid = 0;
    test.ctx.process_id = 0;
    test.ctx.tc = data ? RISCV_IOMMU_DC_TC_PDTV : 0;
    test.walk_fault = RISCV_IOMMU_FQ_CAUSE_WR_FAULT_VS;
    g_assert_cmpuint(trap_write(), ==, MEMTX_ACCESS_ERROR);
    assert_calls(1, 1, 0, 1, 0);
}

typedef struct FallbackCase {
    const char *name;
    unsigned size;
    MemTxResult result;
} FallbackCase;

static void test_fallback(gconstpointer data)
{
    const FallbackCase *cfg = data;

    reset_test();
    test.size = cfg->size;
    test.walk_as = &test.changed_as;
    test.walk_addr = 0x90000040;
    test.dma_result = cfg->result;
    g_assert_cmpuint(trap_write(), ==, cfg->result);
    assert_calls(1, 1, 0, 0, 1);
}

int main(int argc, char **argv)
{
    static const FallbackCase fallback_cases[] = {
        { "fallback-4-success", 4, MEMTX_OK },
        { "fallback-4-error", 4, MEMTX_ERROR },
        { "fallback-8-success", 8, MEMTX_OK },
        { "fallback-8-error", 8, MEMTX_ERROR },
    };

    g_test_init(&argc, &argv, NULL);
    g_test_add_func("/riscv-iommu/trap/unspecified", test_unspecified);
    g_test_add_func("/riscv-iommu/trap/missing-context", test_missing_context);
    g_test_add_func("/riscv-iommu/trap/msi-forward", test_msi_forward);
    g_test_add_data_func("/riscv-iommu/trap/rewalk-fault-pdtv-off", NULL,
                         test_rewalk_fault);
    g_test_add_data_func("/riscv-iommu/trap/rewalk-fault-pdtv-on",
                         GUINT_TO_POINTER(1), test_rewalk_fault);
    for (unsigned i = 0; i < G_N_ELEMENTS(fallback_cases); i++) {
        g_autofree char *path = g_strdup_printf("/riscv-iommu/trap/%s",
                                               fallback_cases[i].name);

        g_test_add_data_func(path, &fallback_cases[i], test_fallback);
    }
    return g_test_run();
}
