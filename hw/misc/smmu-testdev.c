/*
 * A test device for the SMMU
 *
 * This test device is a minimal SMMU-aware device used to test the SMMU.
 *
 * Copyright (c) 2025 Phytium Technology
 *
 * Author:
 *  Tao Tang <tangtao1634@phytium.com.cn>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "qemu/log.h"
#include "qemu/module.h"
#include "qemu/units.h"
#include "system/address-spaces.h"
#include "exec/memattrs.h"
#include "system/dma.h"
#include "hw/pci/pci.h"
#include "hw/pci/pci_device.h"
#include "hw/arm/smmu-common.h"
#include "hw/qdev-properties.h"
#include "qom/object.h"
#include "hw/misc/smmu-testdev.h"

#define TYPE_SMMU_TESTDEV "smmu-testdev"
OBJECT_DECLARE_SIMPLE_TYPE(SMMUTestDevState, SMMU_TESTDEV)

struct SMMUTestDevState {
    PCIDevice parent_obj;
    MemoryRegion bar0;
    uint32_t attr_ns;     /* Track Non-Secure for now; reserve room for more. */

    uint64_t smmu_base;
    uint64_t dma_iova;
    uint32_t dma_len;
    uint32_t dma_dir;
    uint32_t dma_result;
    bool dma_pending;

    /* Future-proof DMA config */
    AddressSpace *dma_as;   /* IOMMU-mediated DMA AS for this device */
    uint32_t dma_mode;      /* 0=legacy pci_dma, 1=attrs via dma_memory_* */
    uint32_t dma_attrs_cfg; /* bit0 secure, bits[2:1] space, bit3 unspecified */

    /* Translation build configuration */
    uint32_t trans_mode;    /* 0=S1, 1=S2, 2=Nested */
    SMMUTestDevSpace s1_space;
    SMMUTestDevSpace s2_space;
    uint32_t trans_status;  /* 0=ok; non-zero=error */

    /* User-configurable BDF (device/function) */
    uint32_t cfg_dev;       /* PCI device/slot number (0..31) */
    uint32_t cfg_fn;        /* PCI function number (0..7) */

    bool debug_log;         /* Enable verbose debug output */
};

/* BAR0 layout */
enum {
    REG_ID            = 0x00,
    REG_ATTR_NS       = 0x04,
    REG_SMMU_BASE_LO  = 0x20,
    REG_SMMU_BASE_HI  = 0x24,
    REG_DMA_IOVA_LO   = 0x28,
    REG_DMA_IOVA_HI   = 0x2C,
    REG_DMA_LEN       = 0x30,
    REG_DMA_DIR       = 0x34,
    REG_DMA_RESULT    = 0x38,
    REG_DMA_DOORBELL  = 0x3C,
    /* Extended controls for DMA attributes/mode (kept after legacy regs) */
    REG_DMA_MODE      = 0x40, /* 0: legacy; 1: attrs path */
    REG_DMA_ATTRS     = 0x44, /* [0] secure, [2:1] space, [3] unspecified */
    /* Translation config & builder */
    REG_TRANS_MODE    = 0x48, /* 0=S1 only, 1=S2 only, 2=Nested */
    REG_S1_SPACE      = 0x4C, /* SMMUTestDevSpace for stage-1 path */
    REG_S2_SPACE      = 0x50, /* SMMUTestDevSpace for stage-2 path */
    REG_TRANS_DBELL   = 0x54, /* bit0=build, bit1=clear status */
    REG_TRANS_STATUS  = 0x58, /* 0=ok else error */
    REG_TRANS_CLEAR   = 0x5C, /* write-any: clear helper-built CD/STE/PTE */
    BAR0_SIZE         = 0x1000,
};

#define DMA_DIR_DEV2HOST     0u
#define DMA_DIR_HOST2DEV     1u
#define DMA_RESULT_IDLE      0xffffffffu
#define DMA_RESULT_BUSY      0xfffffffeu
#define DMA_ERR_BAD_LEN      0xdead0001u
#define DMA_ERR_TX_FAIL      0xdead0002u
#define DMA_MAX_LEN          (64 * KiB)

/*
 * Compile-time calculated STE field setting macros.
 */

 #define STD_STE_OR_CD_ENTRY_BYTES 64 /* 64 bytes per STE entry */
 #define STD_STE_S2T0SZ_VAL 0x14

#define STD_STE_SET_VALID(ste, val)  \
    ((ste)->word[0] = ((ste)->word[0] & ~(0x1 << 0)) | (((val) & 0x1) << 0))
#define STD_STE_SET_CONFIG(ste, val) \
    ((ste)->word[0] = ((ste)->word[0] & ~(0x7 << 1)) | (((val) & 0x7) << 1))
#define STD_STE_SET_S1FMT(ste, val)  \
    ((ste)->word[0] = ((ste)->word[0] & ~(0x3 << 4)) | (((val) & 0x3) << 4))

#define STD_STE_SET_CTXPTR(ste, val)                                      \
do {                                                                      \
    /* Lower address bits (31:6) occupy the upper 26 bits of word[0]. */  \
    (ste)->word[0] = ((ste)->word[0] & 0x0000003FU) |                     \
                     ((uint32_t)(val) & 0xFFFFFFC0U);                     \
                                                                          \
    /* Upper address bits (47:32) occupy the low 16 bits of word[1]. */   \
    (ste)->word[1] = ((ste)->word[1] & 0xFFFF0000U) |                     \
                     ((uint32_t)(((uint64_t)(val)) >> 32) & 0x0000FFFFU); \
} while (0)

#define STD_STE_SET_S1CDMAX(ste, val)  \
    ((ste)->word[1] = ((ste)->word[1] & ~(0x1f << 27)) | (((val) & 0x1f) << 27))
#define STD_STE_SET_S1STALLD(ste, val) \
    ((ste)->word[2] = ((ste)->word[2] & ~(0x1 << 27)) | (((val) & 0x1) << 27))
#define STD_STE_SET_EATS(ste, val)     \
    ((ste)->word[2] = ((ste)->word[2] & ~(0x3 << 28)) | (((val) & 0x3) << 28))
#define STD_STE_SET_STRW(ste, val)     \
    ((ste)->word[2] = ((ste)->word[2] & ~(0x3 << 30)) | (((val) & 0x3) << 30))
#define STD_STE_SET_NSCFG(ste, val)    \
    ((ste)->word[2] = ((ste)->word[2] & ~(0x3 << 14)) | (((val) & 0x3) << 14))
#define STD_STE_SET_S2VMID(ste, val)   \
    ((ste)->word[4] = ((ste)->word[4] & ~0xffff) | ((val) & 0xffff))
#define STD_STE_SET_S2T0SZ(ste, val)   \
    ((ste)->word[5] = ((ste)->word[5] & ~0x3f) | ((val) & 0x3f))
#define STD_STE_SET_S2SL0(ste, val)    \
    ((ste)->word[5] = ((ste)->word[5] & ~(0x3 << 6)) | (((val) & 0x3) << 6))
#define STD_STE_SET_S2TG(ste, val)     \
    ((ste)->word[5] = ((ste)->word[5] & ~(0x3 << 14)) | (((val) & 0x3) << 14))
#define STD_STE_SET_S2PS(ste, val)     \
    ((ste)->word[5] = ((ste)->word[5] & ~(0x7 << 16)) | (((val) & 0x7) << 16))
#define STD_STE_SET_S2AA64(ste, val)   \
    ((ste)->word[5] = ((ste)->word[5] & ~(0x1 << 19)) | (((val) & 0x1) << 19))
#define STD_STE_SET_S2ENDI(ste, val)   \
    ((ste)->word[5] = ((ste)->word[5] & ~(0x1 << 20)) | (((val) & 0x1) << 20))
#define STD_STE_SET_S2AFFD(ste, val)   \
    ((ste)->word[5] = ((ste)->word[5] & ~(0x1 << 21)) | (((val) & 0x1) << 21))
#define STD_STE_SET_S2HD(ste, val)     \
    ((ste)->word[5] = ((ste)->word[5] & ~(0x1 << 23)) | (((val) & 0x1) << 23))
#define STD_STE_SET_S2HA(ste, val)     \
    ((ste)->word[5] = ((ste)->word[5] & ~(0x1 << 24)) | (((val) & 0x1) << 24))
#define STD_STE_SET_S2S(ste, val)      \
    ((ste)->word[5] = ((ste)->word[5] & ~(0x1 << 25)) | (((val) & 0x1) << 25))
#define STD_STE_SET_S2R(ste, val)      \
    ((ste)->word[5] = ((ste)->word[5] & ~(0x1 << 26)) | (((val) & 0x1) << 26))

#define STD_STE_SET_S2TTB(ste, val)                                        \
do {                                                                       \
    /* Lower address bits (31:4) occupy the upper 28 bits of word[6]. */   \
    (ste)->word[6] = ((ste)->word[6] & 0x0000000FU) |                      \
                     ((uint32_t)(val) & 0xFFFFFFF0U);                      \
                                                                           \
    /* Upper address bits (51:32) occupy the low 20 bits of word[7]. */    \
    (ste)->word[7] = ((ste)->word[7] & 0xFFF00000U) |                      \
                     ((uint32_t)(((uint64_t)(val)) >> 32) &                \
                      0x000FFFFFU);                                        \
} while (0)

#define STE_S2TTB(x)                                                       \
    ((extract64((x)->word[7], 0, 16) << 32) |                              \
     ((x)->word[6] & 0xfffffff0))

/*
 * Compile-time calculated CD field setting macros.
 */
#define STD_CD_SET_VALID(cd, val)                                          \
    ((cd)->word[0] = ((cd)->word[0] & ~(0x1 << 31)) |                      \
                     (((val) & 0x1) << 31))
#define STD_CD_SET_TSZ(cd, sel, val)                                       \
    ((cd)->word[0] = ((cd)->word[0] &                                      \
                      ~(0x3F << ((sel) * 16 + 0))) |                       \
                     (((val) & 0x3F) << ((sel) * 16 + 0)))
#define STD_CD_SET_TG(cd, sel, val)                                        \
    ((cd)->word[0] = ((cd)->word[0] &                                      \
                      ~(0x3 << ((sel) * 16 + 6))) |                        \
                     (((val) & 0x3) << ((sel) * 16 + 6)))
#define STD_CD_SET_EPD(cd, sel, val)                                       \
    ((cd)->word[0] = ((cd)->word[0] &                                      \
                      ~(0x1 << ((sel) * 16 + 14))) |                       \
                     (((val) & 0x1) << ((sel) * 16 + 14)))
#define STD_CD_SET_ENDI(cd, val)                                           \
    ((cd)->word[0] = ((cd)->word[0] & ~(0x1 << 15)) |                      \
                     (((val) & 0x1) << 15))
#define STD_CD_SET_IPS(cd, val)                                            \
    ((cd)->word[1] = ((cd)->word[1] & ~(0x7 << 0)) |                       \
                     (((val) & 0x7) << 0))
#define STD_CD_SET_AFFD(cd, val)                                           \
    ((cd)->word[1] = ((cd)->word[1] & ~(0x1 << 3)) |                       \
                     (((val) & 0x1) << 3))
#define STD_CD_SET_HD(cd, val)                                             \
    ((cd)->word[1] = ((cd)->word[1] & ~(0x1 << 10)) |                      \
                     (((val) & 0x1) << 10))
#define STD_CD_SET_HA(cd, val)                                             \
    ((cd)->word[1] = ((cd)->word[1] & ~(0x1 << 11)) |                      \
                     (((val) & 0x1) << 11))
#define STD_CD_SET_TTB(cd, sel, val) do {                                  \
    (cd)->word[(sel) * 2 + 2] = ((cd)->word[(sel) * 2 + 2] & 0x0000000F) | \
                                ((val) & 0xFFFFFFF0);                      \
    (cd)->word[(sel) * 2 + 3] = ((cd)->word[(sel) * 2 + 3] & 0xFFF80000) | \
                                ((((uint64_t)(val)) >> 32) & 0x0007FFFF);  \
} while (0)
#define STD_CD_SET_HAD(cd, sel, val)                                       \
    ((cd)->word[(sel) * 2 + 2] = ((cd)->word[(sel) * 2 + 2] &              \
                                  ~(0x1 << 1)) |                           \
                                 (((val) & 0x1) << 1))
#define STD_CD_SET_MAIR0(cd, val) ((cd)->word[6] = (val))
#define STD_CD_SET_MAIR1(cd, val) ((cd)->word[7] = (val))
#define STD_CD_SET_TCR_T0SZ(cd, val)                                       \
    ((cd)->word[4] = ((cd)->word[4] & ~0x3F) | ((val) & 0x3F))
#define STD_CD_SET_ASID(cd, val)                                           \
    ((cd)->word[1] = ((cd)->word[1] & ~(0xFFFF << 16)) |                   \
                     (((val) & 0xFFFF) << 16))
#define STD_CD_SET_S(cd, val)                                              \
    ((cd)->word[1] = ((cd)->word[1] & ~(0x1 << 12)) |                      \
                     (((val) & 0x1) << 12))
#define STD_CD_SET_R(cd, val)                                              \
    ((cd)->word[1] = ((cd)->word[1] & ~(0x1 << 13)) |                      \
                     (((val) & 0x1) << 13))
#define STD_CD_SET_A(cd, val)                                              \
    ((cd)->word[1] = ((cd)->word[1] & ~(0x1 << 14)) |                      \
                     (((val) & 0x1) << 14))
#define STD_CD_SET_AARCH64(cd, val)                                        \
    ((cd)->word[1] = ((cd)->word[1] & ~(0x1 << 9)) |                       \
                     (((val) & 0x1) << 9))
#define STD_CD_SET_TBI(cd, val)                                            \
    ((cd)->word[1] = ((cd)->word[1] & ~(0x3 << 6)) |                       \
                     (((val) & 0x3) << 6))
#define STD_CD_SET_NSCFG0(cd, val)                                         \
    ((cd)->word[2] = ((cd)->word[2] & ~(0x1 << 0)) | (((val) & 0x1) << 0))
#define STD_CD_SET_NSCFG1(cd, val)                                         \
    ((cd)->word[4] = ((cd)->word[4] & ~(0x1 << 0)) | (((val) & 0x1) << 0))

typedef enum TransMode {
    TM_S1_ONLY = 0,
    TM_S2_ONLY = 1,
    TM_NESTED  = 2,
} TransMode;

/* Minimal STE/CD images (bit layout derived from test helpers) */
typedef struct {
    uint32_t word[8];
} STEImg;

typedef struct {
    uint32_t word[8];
} CDImg;

/* ---- Debug helpers for printing current translation configuration ---- */
static void G_GNUC_PRINTF(2, 3)
smmu_testdev_debug(const SMMUTestDevState *s, const char *fmt, ...)
{
    va_list ap;
    g_autofree char *msg = NULL;

    if (!s->debug_log) {
        return;
    }

    va_start(ap, fmt);
    msg = g_strdup_vprintf(fmt, ap);
    va_end(ap);

    if (qemu_log_enabled()) {
        qemu_log("%s", msg);
    } else {
        fprintf(stderr, "%s", msg);
    }
}

/* Only support Non-Secure space for now. */
static bool smmu_testdev_space_supported(SMMUTestDevSpace sp)
{
    return sp == STD_SPACE_NONSECURE;
}

static MemTxAttrs mk_attrs_from_space(SMMUTestDevSpace space)
{
    MemTxAttrs a = {0};
    if (!smmu_testdev_space_supported(space)) {
        g_assert_not_reached();
    } else {
        a.space = space;
    }
    a.secure = 0;
    return a;
}

/* Convert SMMUTestDevSpace to AddressSpace */
static inline AddressSpace *space_to_as(SMMUTestDevSpace sp)
{
    /* Future work can dispatch Secure/Realm/Root address spaces here. */
    if (!smmu_testdev_space_supported(sp)) {
        g_assert_not_reached();
    }
    return &address_space_memory;
}

/* Apply per-space offset for addresses or values that encode addresses. */
static inline uint64_t std_apply_space_offs(SMMUTestDevSpace sp, uint64_t x)
{
    return x + std_space_offset(sp);
}

/* Direct write helpers (no mirroring) */
static void std_write64(SMMUTestDevSpace sp, uint64_t pa, uint64_t val,
                       uint32_t *status)
{
    MemTxAttrs a = mk_attrs_from_space(sp);
    AddressSpace *as = space_to_as(sp);
    if (!as) {
        *status = 0xdead2011u;
        return;
    }
    MemTxResult r = address_space_write(as, pa, a, &val, sizeof(val));
    if (r != MEMTX_OK && status) {
        *status = 0xdead2011u;
        return;
    }
    *status = 0;
}

static void std_write32(SMMUTestDevSpace sp, uint64_t pa, uint32_t val,
                       uint32_t *status)
{
    MemTxAttrs a = mk_attrs_from_space(sp);
    AddressSpace *as = space_to_as(sp);
    if (!as) {
        *status = 0xdead2012u;
        return;
    }
    MemTxResult r = address_space_write(as, pa, a, &val, sizeof(val));
    if (r != MEMTX_OK && status) {
        *status = 0xdead2012u;
        return;
    }
    *status = 0;
}

/* Build the translation tables with specified stage and security spaces. */
static void smmu_testdev_build_translation(SMMUTestDevState *s)
{
    smmu_testdev_debug(s, "smmu_testdev_build_translation: stage=%s s1_space=%s"
                       " s2_space=%s\n", std_mode_to_str(s->trans_mode),
                       std_space_to_str(s->s1_space),
                       std_space_to_str(s->s2_space));
    uint32_t st = 0;
    SMMUTestDevSpace build_space =
        (s->trans_mode == TM_S1_ONLY) ? s->s1_space : s->s2_space;

    if (!smmu_testdev_space_supported(build_space) ||
        (s->trans_mode != TM_S2_ONLY &&
         !smmu_testdev_space_supported(s->s1_space))) {
        /* Only the Non-Secure space is supported until more domains land. */
        s->trans_status = 0xdead3001u;
        return;
    }

    /*
     * Build base page tables (L0..L3) in the chosen space.
     * For Non-Secure, place tables at Secure-base + space offset and
     * update descriptor values by the same offset to keep internal
     * relationships identical across spaces.
     */
    uint64_t L0_pa = std_apply_space_offs(build_space, STD_L0_ADDR);
    uint64_t L1_pa = std_apply_space_offs(build_space, STD_L1_ADDR);
    uint64_t L2_pa = std_apply_space_offs(build_space, STD_L2_ADDR);
    uint64_t L3_pa = std_apply_space_offs(build_space, STD_L3_ADDR);
    uint64_t L0_val = std_apply_space_offs(build_space, STD_L0_VAL);
    uint64_t L1_val = std_apply_space_offs(build_space, STD_L1_VAL);
    uint64_t L2_val = std_apply_space_offs(build_space, STD_L2_VAL);
    uint64_t L3_val = std_apply_space_offs(build_space, STD_L3_VAL);
    std_write64(build_space, L0_pa, L0_val, &st);
    std_write64(build_space, L1_pa, L1_val, &st);
    std_write64(build_space, L2_pa, L2_val, &st);
    std_write64(build_space, L3_pa, L3_val, &st);

    /* Build STE image */
    STEImg ste = {0};
    switch (s->trans_mode) {
    case TM_S1_ONLY:
        STD_STE_SET_CONFIG(&ste, 0x5);
        break;
    case TM_S2_ONLY:
        STD_STE_SET_CONFIG(&ste, 0x6);
        break;
    case TM_NESTED:
    default:
        STD_STE_SET_CONFIG(&ste, 0x7);
        break;
    }

    uint64_t vttb = STD_VTTB;
    STD_STE_SET_VALID(&ste, 1);
    STD_STE_SET_S2T0SZ(&ste, STD_STE_S2T0SZ_VAL);
    STD_STE_SET_S2SL0(&ste, 0x2);   /* Start level 0*/
    STD_STE_SET_S2TG(&ste, 0);      /* 4KB */
    STD_STE_SET_S2PS(&ste, 0x5);    /* 48 bits */
    STD_STE_SET_S2AA64(&ste, 1);    /* Enable S2AA64 (64-bit address format). */
    STD_STE_SET_S2ENDI(&ste, 0);    /* Little Endian */
    STD_STE_SET_S2AFFD(&ste, 0);    /* AF Fault Disable */

    STD_STE_SET_S2T0SZ(&ste, STD_STE_S2T0SZ_VAL);
    STD_STE_SET_S2SL0(&ste, 0x2);   /* Start level */
    STD_STE_SET_S2TG(&ste, 0);      /* 4KB */
    STD_STE_SET_S2PS(&ste, 0x5);    /* 48 bits ?*/
    /* Set Context Pointer (S1ContextPtr) */
    STD_STE_SET_CTXPTR(&ste, std_apply_space_offs(build_space, STD_CD_GPA));

    STD_STE_SET_S2TTB(&ste, std_apply_space_offs(build_space, vttb));

    /*
     * Start assembling the STE, which is 64 bytes in total.
     */
    for (int i = 0; i < 8; i++) {
        std_write32(build_space,
                    std_apply_space_offs(build_space, STD_STE_GPA) + i * 4,
                    ste.word[i], &st);
        if (st != 0) {
            printf("Writing STE error! status: %x\n", st);
            return;
        }
    }

    /* Build CD image for S1 path if needed */
    if (s->trans_mode != TM_S2_ONLY) {
        CDImg cd = {0};

        STD_CD_SET_ASID(&cd, 0x1e20);     /* ASID */
        STD_CD_SET_AARCH64(&cd, 1);       /* AA64 */
        STD_CD_SET_VALID(&cd, 1);
        STD_CD_SET_A(&cd, 1);
        STD_CD_SET_S(&cd, 0);
        STD_CD_SET_HD(&cd, 0);
        STD_CD_SET_HA(&cd, 0);
        STD_CD_SET_IPS(&cd, 0x4);
        STD_CD_SET_TBI(&cd, 0x0);
        STD_CD_SET_AFFD(&cd, 0x0);
        /* Disable TTB0 translation table walk */
        STD_CD_SET_EPD(&cd, 0, 0x0);
        /* Enable TTB1 translation table walk */
        STD_CD_SET_EPD(&cd, 1, 0x1);
        STD_CD_SET_TSZ(&cd, 0, 0x10);
        STD_CD_SET_TG(&cd, 0, 0x0);
        STD_CD_SET_ENDI(&cd, 0x0);
        STD_CD_SET_NSCFG0(&cd, 0x0);
        STD_CD_SET_NSCFG1(&cd, 0x0);
        STD_CD_SET_R(&cd, 0x1);

        /*
         * CD belongs to S1 path: compute offsets using s1_space so the
         * GPA and embedded addresses are consistent with that space.
         */
        uint64_t cd_ttb = std_apply_space_offs(build_space, vttb);
        smmu_testdev_debug(s, "STD_CD_SET_TTB: 0x%llx\n",
                           (unsigned long long)cd_ttb);
        STD_CD_SET_TTB(&cd, 0, cd_ttb);

        for (int i = 0; i < 8; i++) {
            std_write32(s->s1_space,
                        std_apply_space_offs(s->s1_space, STD_CD_GPA) + i * 4,
                        cd.word[i], &st);
        }

        L3_val = std_apply_space_offs(build_space, STD_L3_S1_VAL);
        std_write64(build_space, L3_pa, L3_val, &st);
    }

    /* Nested extras: CD S2 tables, CD.TTB S2 tables, shared entries. */
    if (s->trans_mode == TM_NESTED) {
        /* CD.S2 tables */
        std_write64(build_space,
                    std_apply_space_offs(build_space, STD_CD_S2_L0_ADDR),
                    std_apply_space_offs(build_space, STD_L0_VAL), &st);
        std_write64(build_space,
                    std_apply_space_offs(build_space, STD_CD_S2_L1_ADDR),
                    std_apply_space_offs(build_space, STD_L1_VAL), &st);
        std_write64(build_space,
                    std_apply_space_offs(build_space, STD_CD_S2_L2_ADDR),
                    std_apply_space_offs(build_space, STD_L2_VAL), &st);
        std_write64(build_space,
                    std_apply_space_offs(build_space, STD_CD_S2_L3_ADDR),
                    std_apply_space_offs(build_space,
                                         STD_CD_S2_L3_VAL), &st);

        /* CD.TTB S2 tables */
        std_write64(build_space,
                    std_apply_space_offs(build_space, STD_CDTTB_S2_L2_ADDR),
                    std_apply_space_offs(build_space, STD_L2_VAL), &st);
        std_write64(build_space,
                    std_apply_space_offs(build_space, STD_CDTTB_S2_L3_ADDR),
                    std_apply_space_offs(build_space,
                                         STD_CDTTB_S2_L3_VAL), &st);

        /* Shared mappings between S1 and S2 page tables */
        std_write64(build_space,
                    std_apply_space_offs(build_space, STD_S1L0_IN_S2L3_ADDR),
                    std_apply_space_offs(build_space,
                                         STD_S1L0_IN_S2L3_VAL), &st);
        std_write64(build_space,
                    std_apply_space_offs(build_space, STD_S1L1_IN_S2L3_ADDR),
                    std_apply_space_offs(build_space,
                                         STD_S1L1_IN_S2L3_VAL), &st);
        std_write64(build_space,
                    std_apply_space_offs(build_space, STD_S1L2_IN_S2L3_ADDR),
                    std_apply_space_offs(build_space,
                                         STD_S1L2_IN_S2L3_VAL), &st);
        std_write64(build_space,
                    std_apply_space_offs(build_space, STD_S1L3_IN_S2L2_ADDR),
                    std_apply_space_offs(build_space,
                                         STD_S1L3_IN_S2L2_VAL), &st);
        std_write64(build_space,
                    std_apply_space_offs(build_space, STD_S1L3_IN_S2L3_ADDR),
                    std_apply_space_offs(build_space,
                                         STD_S1L3_IN_S2L3_VAL), &st);
    }

    s->trans_status = st;
}

/* Manipulate SMMU command to invalidate caches */
static void push_cfgi_cmd(SMMUTestDevState *s,
                          SMMUTestDevSpace bank_sp,
                          uint32_t type,
                          uint32_t sid,
                          bool ssec)
{
    MemTxResult res = 0;
    g_assert(smmu_testdev_space_supported(bank_sp));
    g_assert(!ssec);
    hwaddr bank_off = 0;
    uint32_t base_lo = address_space_ldl_le(&address_space_memory,
                                            s->smmu_base + bank_off + 0x90,
                                            MEMTXATTRS_UNSPECIFIED, &res);
    uint32_t base_hi = address_space_ldl_le(&address_space_memory,
                                            s->smmu_base + bank_off + 0x94,
                                            MEMTXATTRS_UNSPECIFIED, &res);
    uint64_t base = ((uint64_t)base_hi << 32) | base_lo;
    uint32_t log2size = base & 0x1f;
    uint64_t qbase = base & 0xfffffffffffc0ULL;
    uint32_t prod = address_space_ldl_le(&address_space_memory,
                                         s->smmu_base + bank_off + 0x98,
                                         MEMTXATTRS_UNSPECIFIED, &res);
    uint32_t index_mask = (1u << log2size) - 1u;
    uint32_t slot = prod & index_mask;
    uint64_t entry_pa = qbase + (uint64_t)slot * 16u;

    uint32_t words[4] = {0};
    words[0] = (type & 0xff) | (ssec ? (1u << 10) : 0u);
    words[1] = sid;

    /* push command to the command queue */
    MemTxAttrs a = mk_attrs_from_space(bank_sp);
    AddressSpace *as = space_to_as(bank_sp);
    if (!as) {
        printf("push_cfgi_cmd: space %d not supported\n", bank_sp);
        return;
    }
    int ret = address_space_write(as, entry_pa, a,
                                  words, sizeof(words));
    smmu_testdev_debug(s, "push_cfgi_cmd ret %d\n", ret);

    /* update PROD to trigger command handler */
    uint32_t new_prod = (prod + 1) & ((1u << (log2size + 1)) - 1u);
    address_space_stl_le(&address_space_memory,
                         s->smmu_base + bank_off + 0x98,
                         new_prod, MEMTXATTRS_UNSPECIFIED, &res);
    smmu_testdev_debug(s, "last res %d\n", res);
}

/* Clear all the cache to avoid the incorrect cache hits using SMMU commands */
static void smmu_testdev_clear_caches(SMMUTestDevState *s)
{
    uint32_t st = 0;
    static const SMMUTestDevSpace spaces[] = { STD_SPACE_NONSECURE };

    for (size_t idx = 0; idx < ARRAY_SIZE(spaces); idx++) {
        SMMUTestDevSpace build_space = spaces[idx];
        if (!smmu_testdev_space_supported(build_space)) {
            continue;
        }
        /* Clear L0..L3 entries written by the builder. */
        uint64_t L0_pa = std_apply_space_offs(build_space, STD_L0_ADDR);
        uint64_t L1_pa = std_apply_space_offs(build_space, STD_L1_ADDR);
        uint64_t L2_pa = std_apply_space_offs(build_space, STD_L2_ADDR);
        uint64_t L3_pa = std_apply_space_offs(build_space, STD_L3_ADDR);
        uint64_t S2_L0_pa = std_apply_space_offs(build_space,
                                                 STD_CD_S2_L0_ADDR);
        uint64_t S2_L1_pa = std_apply_space_offs(build_space,
                                                 STD_CD_S2_L1_ADDR);
        uint64_t S2_L2_pa = std_apply_space_offs(build_space,
                                                 STD_CD_S2_L2_ADDR);
        uint64_t S2_L3_pa = std_apply_space_offs(build_space,
                                                 STD_CD_S2_L3_ADDR);

        std_write64(build_space, L0_pa, 0ull, &st);
        std_write64(build_space, L1_pa, 0ull, &st);
        std_write64(build_space, L2_pa, 0ull, &st);
        std_write64(build_space, L3_pa, 0ull, &st);
        std_write64(build_space, S2_L0_pa, 0ull, &st);
        std_write64(build_space, S2_L1_pa, 0ull, &st);
        std_write64(build_space, S2_L2_pa, 0ull, &st);
        std_write64(build_space, S2_L3_pa, 0ull, &st);

        /* Clear STE image where it was placed. */
        for (int i = 0; i < 8; i++) {
            std_write32(build_space,
                        std_apply_space_offs(build_space, STD_STE_GPA) + i * 4,
                        0u, &st);
        }

        /* Clear CD image in S1 space (matches builder placement). */
        for (int i = 0; i < 8; i++) {
            std_write32(build_space,
                        std_apply_space_offs(build_space, STD_CD_GPA) + i * 4,
                        0u, &st);
        }
    }

    /* Invalidate configuration cache via CFGI_STE and CFGI_CD commands */
    if (s->smmu_base) {
        /* Compute this PCI function's StreamID: bus 0, current devfn. */
        uint8_t devfn = PCI_DEVICE(&s->parent_obj)->devfn;
        uint32_t sid = PCI_BUILD_BDF(0, devfn);

        /* Non-secure bank invalidations (SSEC=0). */
        push_cfgi_cmd(s, STD_SPACE_NONSECURE, STD_CMD_CFGI_STE, sid, false);
        push_cfgi_cmd(s, STD_SPACE_NONSECURE, STD_CMD_CFGI_CD,  sid, false);
        push_cfgi_cmd(s, STD_SPACE_NONSECURE, STD_CMD_TLBI_NSNH_ALL,
                      sid, false);

        /* Add Secure/Realm/Root invalidations here once those domains exist. */
    }
}

static void smmu_testdev_refresh_attrs(SMMUTestDevState *s)
{
    /* Report the baked-in Non-Secure attributes until more exist. */
    s->attr_ns = (STD_SPACE_NONSECURE << 1);
}

/* Trigger a DMA operation */
static void smmu_testdev_maybe_run_dma(SMMUTestDevState *s)
{
    if (!s->dma_pending) {
        return;
    }
    smmu_testdev_debug(s, "smmu_testdev_maybe_run_dma: dma_pending: %d\n",
                       s->dma_pending);

    s->dma_pending = false;

    if (!s->dma_len || s->dma_len > DMA_MAX_LEN) {
        s->dma_result = DMA_ERR_BAD_LEN;
        return;
    }

    g_autofree uint8_t *buf = g_malloc(s->dma_len);
    MemTxResult res;

    if (s->dma_mode == 0) {
        if (s->dma_dir == DMA_DIR_HOST2DEV) {
            res = pci_dma_read(PCI_DEVICE(&s->parent_obj), s->dma_iova,
                               buf, s->dma_len);
        } else {
            for (uint32_t i = 0; i < s->dma_len; i++) {
                buf[i] = 0xA0u + (i & 0x1fu);
            }
            res = pci_dma_write(PCI_DEVICE(&s->parent_obj), s->dma_iova,
                                buf, s->dma_len);
        }
    } else {
        SMMUTestDevSpace dma_space =
            (SMMUTestDevSpace)((s->dma_attrs_cfg >> 1) & 0x3);
        if (!smmu_testdev_space_supported(dma_space)) {
            /* Default to Non-Secure until other spaces are modeled. */
            dma_space = STD_SPACE_NONSECURE;
        }
        MemTxAttrs attrs = {
            .secure = 0,
            .space = dma_space,
            .unspecified = (s->dma_attrs_cfg & (1u << 3)) ? 1 : 0,
        };
        /*
         * If 'unspecified' is set, bypass IOMMU AS and use system memory.
         * This helps tests that want deterministic success without full
         * IOMMU programming.
         */
        AddressSpace *as = (s->dma_as && !attrs.unspecified)
                               ? s->dma_as
                               : &address_space_memory;
        if (s->dma_dir == DMA_DIR_HOST2DEV) {
            res = dma_memory_read(as, s->dma_iova, buf, s->dma_len, attrs);
        } else {
            for (uint32_t i = 0; i < s->dma_len; i++) {
                buf[i] = 0xA0u + (i & 0x1fu);
            }
            res = dma_memory_write(as, s->dma_iova, buf, s->dma_len, attrs);
        }
    }
    s->dma_result = (res == MEMTX_OK) ? 0 : DMA_ERR_TX_FAIL;
    smmu_testdev_debug(s, "iommu ret %d , dma_result: 0x%x\n",
                       res, s->dma_result);
}

static uint64_t smmu_testdev_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    SMMUTestDevState *s = opaque;
    switch (addr) {
    case REG_ID:
        /*
         * Only reads of REG_ID intentionally trigger the side effects
         * (SMMU CR0 write and pending DMA). This lets tests poll
         * REG_DMA_RESULT to observe BUSY before consuming the DMA.
         */
        smmu_testdev_maybe_run_dma(s);
        return 0x53544d4du; /* 'STMM' */
    case REG_ATTR_NS:
        return s->attr_ns;
    case REG_SMMU_BASE_LO:
        return (uint32_t)(s->smmu_base & 0xffffffffu);
    case REG_SMMU_BASE_HI:
        return (uint32_t)(s->smmu_base >> 32);
    case REG_DMA_IOVA_LO:
        return (uint32_t)(s->dma_iova & 0xffffffffu);
    case REG_DMA_IOVA_HI:
        return (uint32_t)(s->dma_iova >> 32);
    case REG_DMA_LEN:
        return s->dma_len;
    case REG_DMA_DIR:
        return s->dma_dir;
    case REG_DMA_RESULT:
        return s->dma_result;
    case REG_DMA_MODE:
        return s->dma_mode;
    case REG_DMA_ATTRS:
        return s->dma_attrs_cfg;
    case REG_TRANS_MODE:
        return s->trans_mode;
    case REG_S1_SPACE:
        return s->s1_space;
    case REG_S2_SPACE:
        return s->s2_space;
    case REG_TRANS_STATUS:
        return s->trans_status;
    default:
        return 0;
    }
}

static void smmu_testdev_mmio_write(void *opaque, hwaddr addr, uint64_t val,
                                    unsigned size)
{
    SMMUTestDevState *s = opaque;
    uint32_t data = val;

    switch (addr) {
    case REG_ID:
        if (data == 0x1) {
            smmu_testdev_refresh_attrs(s);
        }
        break;
    case REG_SMMU_BASE_LO:
        s->smmu_base = (s->smmu_base & ~0xffffffffull) | data;
        break;
    case REG_SMMU_BASE_HI:
        s->smmu_base = (s->smmu_base & 0xffffffffull) |
                       ((uint64_t)data << 32);
        break;
    case REG_DMA_IOVA_LO:
        s->dma_iova = (s->dma_iova & ~0xffffffffull) | data;
        break;
    case REG_DMA_IOVA_HI:
        s->dma_iova = (s->dma_iova & 0xffffffffull) |
                      ((uint64_t)data << 32);
        break;
    case REG_DMA_LEN:
        s->dma_len = data;
        break;
    case REG_DMA_DIR:
        s->dma_dir = data ? DMA_DIR_HOST2DEV : DMA_DIR_DEV2HOST;
        break;
    case REG_DMA_RESULT:
        s->dma_result = data;
        break;
    case REG_DMA_DOORBELL:
        if (data & 0x1) {
            s->dma_pending = true;
            s->dma_result = DMA_RESULT_BUSY;
        } else {
            s->dma_pending = false;
            s->dma_result = DMA_RESULT_IDLE;
        }
        break;
    case REG_DMA_MODE:
        s->dma_mode = data & 0x1;
        break;
    case REG_DMA_ATTRS:
        s->dma_attrs_cfg = data;
        break;
    case REG_TRANS_MODE:
        s->trans_mode = data & 0x3;
        break;
    case REG_S1_SPACE:
        s->s1_space = (SMMUTestDevSpace)(data & 0x3);
        break;
    case REG_S2_SPACE:
        s->s2_space = (SMMUTestDevSpace)(data & 0x3);
        break;
    case REG_TRANS_DBELL:
        if (data & 0x2) {
            s->trans_status = 0;
        }
        if (data & 0x1) {
            smmu_testdev_build_translation(s);
        }
        break;
    case REG_TRANS_CLEAR:
        /* Clear helper caches so the next iteration rebuilds cleanly. */
        smmu_testdev_clear_caches(s);
        break;
    default:
        break;
    }
}

static const MemoryRegionOps smmu_testdev_mmio_ops = {
    .read = smmu_testdev_mmio_read,
    .write = smmu_testdev_mmio_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = {
        .min_access_size = 4,
        .max_access_size = 4,
    },
};

static void smmu_testdev_realize(PCIDevice *pdev, Error **errp)
{
    SMMUTestDevState *s = SMMU_TESTDEV(pdev);

    /* Apply user-configurable BDF (default 0:1). */
    uint8_t dev = s->cfg_dev & 0x1f;
    uint8_t fn  = s->cfg_fn & 0x7;
    pdev->devfn = (dev << 3) | fn;

    smmu_testdev_refresh_attrs(s);
    s->smmu_base = 0;
    s->dma_iova = 0;
    s->dma_len = 0;
    s->dma_dir = DMA_DIR_DEV2HOST;
    s->dma_result = DMA_RESULT_IDLE;
    s->dma_pending = false;
    s->dma_mode = 0;
    s->dma_attrs_cfg = 0;
    s->dma_as = pci_device_iommu_address_space(pdev);
    s->trans_mode = TM_S2_ONLY;
    s->s1_space = STD_SPACE_NONSECURE;
    s->s2_space = STD_SPACE_NONSECURE;
    s->trans_status = 0;

    if (s->debug_log) {
        smmu_testdev_debug(s, "[smmu-testdev] debug logging enabled\n");
    }

    memory_region_init_io(&s->bar0, OBJECT(pdev), &smmu_testdev_mmio_ops, s,
                          TYPE_SMMU_TESTDEV ".bar0", BAR0_SIZE);
    pci_register_bar(pdev, 0, PCI_BASE_ADDRESS_SPACE_MEMORY, &s->bar0);
}

static void smmu_testdev_reset(DeviceState *dev)
{
    SMMUTestDevState *s = SMMU_TESTDEV(dev);

    smmu_testdev_refresh_attrs(s);
    s->smmu_base = 0;
    s->dma_iova = 0;
    s->dma_len = 0;
    s->dma_dir = DMA_DIR_DEV2HOST;
    s->dma_result = DMA_RESULT_IDLE;
    s->dma_pending = false;
    s->dma_mode = 0;
    s->dma_attrs_cfg = 0;
    s->trans_mode = TM_S2_ONLY;
    s->s1_space = STD_SPACE_NONSECURE;
    s->s2_space = STD_SPACE_NONSECURE;
    s->trans_status = 0;
    /* Keep cfg_dev/cfg_fn as-is across reset */
}

static const Property smmu_testdev_properties[] = {
    DEFINE_PROP_UINT32("device", SMMUTestDevState, cfg_dev, 0),
    DEFINE_PROP_UINT32("function", SMMUTestDevState, cfg_fn, 1),
    DEFINE_PROP_BOOL("debug-log", SMMUTestDevState, debug_log, false),
};

static void smmu_testdev_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    PCIDeviceClass *pc = PCI_DEVICE_CLASS(klass);

    pc->realize = smmu_testdev_realize;
    pc->vendor_id = PCI_VENDOR_ID_REDHAT;
    pc->device_id = PCI_DEVICE_ID_REDHAT_TEST;
    pc->revision = 0;
    pc->class_id = PCI_CLASS_OTHERS;
    dc->desc = "A test device for the SMMU";
    set_bit(DEVICE_CATEGORY_MISC, dc->categories);
    device_class_set_legacy_reset(dc, smmu_testdev_reset);
    device_class_set_props(dc, smmu_testdev_properties);
}

static void smmu_testdev_instance_init(Object *obj)
{
    SMMUTestDevState *s = SMMU_TESTDEV(obj);
    s->cfg_dev = 0;
    s->cfg_fn = 1; /* default StreamID = 1 (slot 0, fn 1) */
    s->debug_log = false;
}

static const TypeInfo smmu_testdev_info = {
    .name          = TYPE_SMMU_TESTDEV,
    .parent        = TYPE_PCI_DEVICE,
    .instance_size = sizeof(SMMUTestDevState),
    .instance_init = smmu_testdev_instance_init,
    .class_init    = smmu_testdev_class_init,
    .interfaces    = (const InterfaceInfo[]) {
        { INTERFACE_CONVENTIONAL_PCI_DEVICE },
        { }
    },
};

static void smmu_testdev_register_types(void)
{
    type_register_static(&smmu_testdev_info);
}

type_init(smmu_testdev_register_types);
