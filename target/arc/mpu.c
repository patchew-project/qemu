/*
 * QEMU ARC CPU
 *
 * Copyright (c) 2020 Synppsys Inc.
 * Contributed by Shahab Vahedi (Synopsys)
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, see
 * http://www.gnu.org/licenses/lgpl-2.1.html
 */

#include "qemu/osdep.h"
#include "mpu.h"
#include "cpu.h"
#include "exec/exec-all.h"
#include "mmu.h"

/*
 * In case of exception, this signals the effective region
 * was the default one
 */
#define MPU_DEFAULT_REGION_NR 0xff

/* Defines used by in-house functions */
#define MPU_EN_EN_BIT   30
#define MPU_EN_KR_BIT    8
#define MPU_EN_KW_BIT    7
#define MPU_EN_KE_BIT    6
#define MPU_EN_UR_BIT    5
#define MPU_EN_UW_BIT    4
#define MPU_EN_UE_BIT    3

#define MPU_ECR_EC_CODE_BIT 16
#define MPU_ECR_VT_BIT       8

#define MPU_BASE_ADDR_MASK  0xffffffe0  /* ignore least 5 bits */
#define MPU_BASE_VALID_MASK 0x00000001  /* bit #0 */

/*
 * Given a number of bits as width, calc the mask to
 * "and" with. e.g.: 3 bits --> 8 - 1 --> 7 (111b)
 */
#define MPU_WIDTH_TO_MASK(w) ((1 << (w)) - 1)
#define MPU_PERMS_REG_LOWER_SIZE_WIDTH  2
#define MPU_PERMS_REG_HIGHER_SIZE_WIDTH 3
#define MPU_PERMS_REG_HIGHER_SIZE_POS   9

/*
 * After knowing the operating mode (user/kernel),
 * this struct represents the effective permissions.
 */
typedef struct MPUEffectPerm {
    bool read;
    bool write;
    bool exec;
} MPUEffectPerm;

/* Packer and unpackers (local to this translation unit) */
static inline uint32_t pack_enable(const bool ena)
{
    return ena << MPU_EN_EN_BIT;
}

static inline void unpack_enable(bool *enabled, uint32_t value)
{
    *enabled = (value >> MPU_EN_EN_BIT) & 1;
}

static inline uint32_t pack_permissions(const MPUPermissions *perms)
{
    return perms->KR << MPU_EN_KR_BIT |
           perms->KW << MPU_EN_KW_BIT |
           perms->KE << MPU_EN_KE_BIT |
           perms->UR << MPU_EN_UR_BIT |
           perms->UW << MPU_EN_UW_BIT |
           perms->UE << MPU_EN_UE_BIT;
}

static inline void unpack_permissions(MPUPermissions *perms, uint32_t value)
{
    perms->KR = (value >> MPU_EN_KR_BIT) & 1;
    perms->KW = (value >> MPU_EN_KW_BIT) & 1;
    perms->KE = (value >> MPU_EN_KE_BIT) & 1;
    perms->UR = (value >> MPU_EN_UR_BIT) & 1;
    perms->UW = (value >> MPU_EN_UW_BIT) & 1;
    perms->UE = (value >> MPU_EN_UE_BIT) & 1;
}

static inline uint32_t pack_enable_reg(const MPUEnableReg *mpuen)
{
    return pack_enable(mpuen->enabled) |
           pack_permissions(&mpuen->permission);
}

static inline void unpack_enable_reg(MPUEnableReg *mpuen, uint32_t value)
{
    unpack_enable(&mpuen->enabled, value);
    unpack_permissions(&mpuen->permission, value);
}

static inline uint32_t pack_ecr(const MPUECR *mpuecr)
{
    return ARC_MPU_ECR_VEC_NUM     << MPU_ECR_EC_CODE_BIT |
           (mpuecr->violation & 3) << MPU_ECR_VT_BIT      |
           mpuecr->region;
}

static inline uint32_t pack_base_reg(const MPUBaseReg *mpurdb)
{
    return mpurdb->addr | mpurdb->valid;
}

static inline void unpack_base_reg(MPUBaseReg *mpurdb, uint32_t value)
{
    mpurdb->addr  = value & MPU_BASE_ADDR_MASK;
    mpurdb->valid = value & MPU_BASE_VALID_MASK;
}


/*
 * Break the "size" field into "higher" and "lower" parts
 * e.g.: a b c d e --> a b c . . . d e
 *                     higher     lower
 */
static uint32_t pack_region_size_bits(uint8_t size_bits)
{
    uint32_t lower =
        size_bits & MPU_WIDTH_TO_MASK(MPU_PERMS_REG_LOWER_SIZE_WIDTH);
    uint32_t higher = size_bits >> MPU_PERMS_REG_LOWER_SIZE_WIDTH;
    higher &= MPU_WIDTH_TO_MASK(MPU_PERMS_REG_HIGHER_SIZE_WIDTH);
    return (higher << MPU_PERMS_REG_HIGHER_SIZE_POS) | lower;
}

/*
 * Put the higher and lower parts of "size" field together
 * e.g.: a b c . . . d e ---> abcde
 *       higher     lower
 */
static void unpack_region_size_bits(uint8_t *size_bits, uint32_t value)
{
    uint8_t lower =
        value & MPU_WIDTH_TO_MASK(MPU_PERMS_REG_LOWER_SIZE_WIDTH);
    uint8_t higher = value >> MPU_PERMS_REG_HIGHER_SIZE_POS;
    higher &= MPU_WIDTH_TO_MASK(MPU_PERMS_REG_HIGHER_SIZE_WIDTH);
    *size_bits = (higher << MPU_PERMS_REG_LOWER_SIZE_WIDTH) | lower;
}

static void set_region_mask(uint32_t *mask, uint8_t size_bits)
{
    uint32_t region_offset_mask = 0;
    /*
     * size_bits: 00100b (4)  --> 32 bytes --> least 5  bits are 0
     * size_bits: 00101b (5)  --> 64 bytes --> least 6  bits are 0
     * ...
     * size_bits: 11111b (31) --> 4 gb     --> least 32 bits are 0
     */
    if (size_bits >= 4 && size_bits < 31) {
        region_offset_mask = (2 << size_bits) - 1;
    } else if (size_bits == 31) {
        region_offset_mask = 0xffffffff;
    } else {
      qemu_log_mask(LOG_GUEST_ERROR, "[MPU] %hu as size of a region is "
                                     "undefined behaviour.\n", size_bits);
    }
    *mask = ~region_offset_mask;
}

static inline uint32_t pack_perm_reg(const MPUPermReg *mpurdp)
{
    return pack_region_size_bits(mpurdp->size_bits) |
           pack_permissions(&mpurdp->permission);
}

static void unpack_perm_reg(MPUPermReg *mpurdp, uint32_t value)
{
    unpack_region_size_bits(&mpurdp->size_bits, value);
    /* size_bits of below 4 are undefined --> Assuming min region size. */
    mpurdp->size = (mpurdp->size_bits < 4) ? 32 : (2ul << mpurdp->size_bits);
    unpack_permissions(&mpurdp->permission, value);
    /* The mask is a facilitator to find the corresponding region easier */
    set_region_mask(&mpurdp->mask, mpurdp->size_bits);
}


/* Extern function: To be called at reset() */
void arc_mpu_init(struct ARCCPU *cpu)
{
    static const MPUPermissions INITIAL_PERMS = {0};
    ARCMPU *mpu = &cpu->env.mpu;
    size_t idx = 0;

    /* Maybe the version must be determinded also based on CPU type */
    mpu->reg_bcr.version = cpu->cfg.has_mpu ? ARC_MPU_VERSION : 0;
    mpu->reg_bcr.regions = cpu->cfg.has_mpu ? cpu->cfg.mpu_num_regions : 0;
    switch (mpu->reg_bcr.regions) {
    case 0 ... 2:
    case 4:
    case 8:
    case 16:
        break;
    default:
        assert(!"Invalid number of MPU regions.");
    }

    /*
     * We use this flag to determine if MPU is in motion or not.
     * This is most of the time the same as reg_enable.enabled,
     * However, in case of a double exception (Machine Check)
     * this becomes false while reg_enable.enabled holds its
     * value. As a result, there is no MPU anymore after a
     * Machine Check is raised.
     */
    mpu->enabled = false;

    mpu->reg_enable.enabled    = false;
    mpu->reg_enable.permission = INITIAL_PERMS;

    mpu->reg_ecr.region    = 0;
    mpu->reg_ecr.violation = 0;
    mpu->exception.number  = ARC_MPU_ECR_VEC_NUM;
    mpu->exception.code    = 0;
    mpu->exception.param   = ARC_MPU_ECR_PARAM;

    for (idx = 0; idx < ARC_MPU_MAX_NR_REGIONS; ++idx) {
        mpu->reg_base[idx].valid = false;
        mpu->reg_base[idx].addr  = 0;

        mpu->reg_perm[idx].size_bits  = 0;
        mpu->reg_perm[idx].mask       = 0xffffffff;
        mpu->reg_perm[idx].permission = INITIAL_PERMS;
    }
}

/* Checking the sanity of situation before accessing MPU registers */
static void validate_mpu_regs_access(CPUARCState *env)
{
    /* MPU registers are only accessible in kernel mode */
    if (is_user_mode(env)) {
        arc_raise_exception(env, EXCP_PRIVILEGEV);
    }
    /* No MPU, no getting any */
    else if ((env_archcpu(env))->cfg.has_mpu == false) {
        arc_raise_exception(env, EXCP_INST_ERROR);
    }
}

/* If 'rgn' is higher than configured region number, throw an exception */
static inline void validate_region_number(const ARCMPU *mpu, uint8_t rgn)
{
    if (!(rgn < mpu->reg_bcr.regions)) {
        arc_raise_exception(container_of(mpu, CPUARCState, mpu), /* env */
                                         EXCP_INST_ERROR);
    }
}

/* Extern function: Getter for MPU registers */
uint32_t
arc_mpu_aux_get(const struct arc_aux_reg_detail *aux_reg_detail, void *data)
{
    validate_mpu_regs_access((CPUARCState *) data);
    ARCMPU *mpu = &(((CPUARCState *) data)->mpu);
    uint32_t reg = 0;

    switch (aux_reg_detail->id) {
    case AUX_ID_mpu_build:
        reg = (mpu->reg_bcr.regions << 8) | mpu->reg_bcr.version;
        break;
    case AUX_ID_mpuen:
        reg = pack_enable_reg(&mpu->reg_enable);
        break;
    case AUX_ID_mpuic:
        reg = pack_ecr(&mpu->reg_ecr);
        break;
    case AUX_ID_mpurdb0 ... AUX_ID_mpurdb15: {
        const uint8_t rgn = aux_reg_detail->id - AUX_ID_mpurdb0;
        validate_region_number(mpu, rgn);
        reg = pack_base_reg(&mpu->reg_base[rgn]);
        break;
    }
    case AUX_ID_mpurdp0 ... AUX_ID_mpurdp15: {
        const uint8_t rgn = aux_reg_detail->id - AUX_ID_mpurdp0;
        validate_region_number(mpu, rgn);
        reg = pack_perm_reg(&mpu->reg_perm[rgn]);
        break;
    }
    default:
        g_assert_not_reached();
    }
    return reg;
}

/* Log the MPU sensitive information */
static void log_mpu_data(const ARCMPU *mpu)
{
    char suffix[4] = " B";
    uint32_t size;
    /* Log header */
    qemu_log_mask(CPU_LOG_MMU,
            "[MPU] ,--------.-------.------------.--------.---"
            "--------------------.--------------.------------.\n");
    qemu_log_mask(CPU_LOG_MMU,
            "[MPU] | region | valid |  address   |  size  |   "
            "effective address   | kernel perm. | user perm. |\n");
    qemu_log_mask(CPU_LOG_MMU,
            "[MPU] |--------+-------+------------+--------+---"
            "--------------------+--------------+------------|\n");
    /* Now its every regions turn */
    for (uint8_t r = 0; r < mpu->reg_bcr.regions; ++r) {
        const MPUBaseReg *rb = &mpu->reg_base[r];
        const MPUPermReg *rp = &mpu->reg_perm[r];
        const MPUPermissions *p = &rp->permission;
        if (rp->size >= 0x40000000) {
            suffix[0] = 'G';
            size = rp->size >> 30;
        } else if (rp->size >= 0x00100000) {
            suffix[0] = 'M';
            size = rp->size >> 20;
        } else if (rp->size >= 0x00000400) {
            suffix[0] = 'K';
            size = rp->size >> 10;
        } else {
            suffix[0] = ' ';
            size = rp->size & 0x3FF;
        }
        qemu_log_mask(CPU_LOG_MMU,
                "[MPU] |   %02u   | %s | 0x%08x | %3u %s | 0x%08x-0x%08x "
                "|     %c%c%c      |    %c%c%c     |\n",
                r, rb->valid ? "true " : "false", rb->addr, size, suffix,
                rb->addr & rp->mask,
                (rb->addr & rp->mask) + ((uint32_t) rp->size),
                p->KR ? 'r' : '-', p->KW ? 'w' : '-', p->KE ? 'x' : '-',
                p->UR ? 'r' : '-', p->UW ? 'w' : '-', p->UE ? 'x' : '-');
    }
    /* Default region */
    const MPUPermissions *defp = &mpu->reg_enable.permission;
    qemu_log_mask(CPU_LOG_MMU,
            "[MPU] |  def.  |       |            |        |          "
            "             |     %c%c%c      |    %c%c%c     |\n",
            defp->KR ? 'r' : '-', defp->KW ? 'w' : '-', defp->KE ? 'x' : '-',
            defp->UR ? 'r' : '-', defp->UW ? 'w' : '-', defp->UE ? 'x' : '-');
    /* Wrap it up */
    qemu_log_mask(CPU_LOG_MMU,
            "[MPU] `--------^-------^------------^--------^---"
            "--------------------^--------------^------------'\n");
}

/* Extern function: Setter for MPU registers */
void
arc_mpu_aux_set(const struct arc_aux_reg_detail *aux_reg_detail,
                const uint32_t value, void *data)
{
    validate_mpu_regs_access((CPUARCState *) data);
    ARCMPU *mpu = &(((CPUARCState *) data)->mpu);

    switch (aux_reg_detail->id) {
    case AUX_ID_mpuen:
        unpack_enable_reg(&mpu->reg_enable, value);
        mpu->enabled = mpu->reg_enable.enabled;
        break;
    case AUX_ID_mpurdb0 ... AUX_ID_mpurdb15: {
        const uint8_t rgn = aux_reg_detail->id - AUX_ID_mpurdb0;
        validate_region_number(mpu, rgn);
        unpack_base_reg(&mpu->reg_base[rgn], value);
        break;
    }
    case AUX_ID_mpurdp0 ... AUX_ID_mpurdp15: {
        const uint8_t rgn = aux_reg_detail->id - AUX_ID_mpurdp0;
        validate_region_number(mpu, rgn);
        unpack_perm_reg(&mpu->reg_perm[rgn], value);
        break;
    }
    default:
        g_assert_not_reached();
    }
    /* Invalidate the entries in qemu's translation buffer */
    tlb_flush(env_cpu((CPUARCState *) data));
    /* If MPU is enabled, log its data */
    if (mpu->enabled) {
        log_mpu_data(mpu);
    }
}

/*
 * If user mode, return the user permission only.
 * If kernel mode, return the aggregation of both permissions.
 */
static void get_effective_rwe(const MPUPermissions *perm,
                              bool user_mode, MPUEffectPerm *effective)
{
    effective->read  = user_mode ? perm->UR : perm->KR | perm->UR;
    effective->write = user_mode ? perm->UW : perm->KW | perm->UW;
    effective->exec  = user_mode ? perm->UE : perm->KE | perm->UE;
}

/* Translate internal QEMU's access type to an MPU violation type */
static inline uint8_t qemu_access_to_violation(MMUAccessType access)
{
    uint8_t vt = 0;
    switch (access) {
    case MMU_INST_FETCH:
        vt = MPU_CAUSE_FETCH;
        break;
    case MMU_DATA_LOAD:
        vt = MPU_CAUSE_READ;
        break;
    case MMU_DATA_STORE:
        vt = MPU_CAUSE_WRITE;
        break;
    /* TODO: there must be an MPU_CAUSE_RW as well, but how? */
    default:
        g_assert_not_reached();
    }
    return vt;
}

/* Translate MPU's permission to QEMU's tlb permission */
static inline uint8_t mpu_permission_to_qemu(const MPUPermissions *perm,
                                             bool user_mode)
{
    MPUEffectPerm effective = { };
    get_effective_rwe(perm, user_mode, &effective);
    return (effective.read  ? PAGE_READ  : 0) |
           (effective.write ? PAGE_WRITE : 0) |
           (effective.exec  ? PAGE_EXEC  : 0);
}

/*
 * Check if the 'access' is according to 'perm'ission.
 * Note that a user mode permission is also implied for kernel.
 */
static bool allowed(MMUAccessType access, bool user_mode,
                    const MPUPermissions *perm)
{
    MPUEffectPerm effective_perm = { };
    get_effective_rwe(perm, user_mode, &effective_perm);

    switch (access) {
    case MMU_INST_FETCH:
        return effective_perm.exec;
    case MMU_DATA_LOAD:
        return effective_perm.read;
    case MMU_DATA_STORE:
        return effective_perm.write;
    default:
        g_assert_not_reached();
    }
}

/* Used for logging purposes */
static inline const char *log_violation_to_str(uint8_t violation)
{
    return (violation == MPU_CAUSE_FETCH) ? "fetch"      :
           (violation == MPU_CAUSE_READ)  ? "read"       :
           (violation == MPU_CAUSE_WRITE) ? "write"      :
           (violation == MPU_CAUSE_RW)    ? "read-write" :
           "unknown";
}

/* Sets the exception data */
static void set_exception(CPUARCState *env, uint32_t addr,
                          uint8_t region, MMUAccessType access)
{
    MPUECR *ecr = &env->mpu.reg_ecr;
    ecr->violation = qemu_access_to_violation(access);
    ecr->region = region;

    /* this info is used by the caller to trigger the exception */
    MPUException *excp = &env->mpu.exception;
    excp->number = EXCP_PROTV;
    excp->code = ecr->violation;
    excp->param = ARC_MPU_ECR_PARAM;

    qemu_log_mask(CPU_LOG_MMU,
            "[MPU] exception: region=%hu, addr=0x%08x, violation=%s\n",
            region, addr, log_violation_to_str(ecr->violation));
}

/*
 * Given an 'addr', finds the region it belongs to. If no match
 * is found, then it signals this by returning MPU_DEFAULT_REGION_NR.
 * Since regions with lower index has higher priority, the first match
 * is the correct one even if there is overlap among regions.
 */
static uint8_t get_matching_region(const ARCMPU *mpu, uint32_t addr)
{
    qemu_log_mask(CPU_LOG_MMU, "[MPU] looking up: addr=0x%08x\n", addr);
    for (uint8_t r = 0; r < mpu->reg_bcr.regions; ++r) {
        if (!mpu->reg_base[r].valid) {
            continue;
        }
        const uint32_t mask = mpu->reg_perm[r].mask;
        /* 'addr' falls under the current region? */
        if ((mpu->reg_base[r].addr & mask) == (addr & mask)) {
            qemu_log_mask(CPU_LOG_MMU,
                    "[MPU] region match: region=%u, base=0x%08x\n",
                    r, mpu->reg_base[r].addr);
            return r;
        }
    }
    /* If we are here, then no corresponding region is found */
    qemu_log_mask(CPU_LOG_MMU, "[MPU] default region will be used.\n");
    return MPU_DEFAULT_REGION_NR;
}

/*
 * Returns the corresponding permission for the given 'region'.
 * If 'region' is MPU_DEFAULT_REGION_NR, then the default permission
 * from MPU_EN register is returned.
 */
static const MPUPermissions *get_permission(const ARCMPU *mpu,
                                            uint8_t region)
{
    if (region < mpu->reg_bcr.regions) {
        return &mpu->reg_perm[region].permission;
    } else if (region == MPU_DEFAULT_REGION_NR) {
        return &mpu->reg_enable.permission;
    }
    g_assert_not_reached();
}

/*
 * Have the following example in mind:
 * ,------------.
 * | region 5   |
 * |            |
 * |            | first page of region 5
 * |            |
 * |............|
 * |            |
 * |,----------.| second page of region 5
 * || region 4 ||
 * |`----------'|
 * `------------'
 * Here region four's size is half a page size.
 *
 * This function checks if the page that the address belongs to,
 * overlaps with another higher priority region. regions with lower
 * priority don't matter because they cannot influence the permission.
 *
 * The logic is to check if any of the valid regions is contained in
 * the page that 'addr' belongs to.
 */
static bool is_overlap_free(const ARCMPU *mpu, target_ulong addr,
                            uint8_t current_region)
{
    /* Nothing has higher priority than region 0 */
    if (current_region == 0) {
        return true;
    } else if (current_region == MPU_DEFAULT_REGION_NR) {
        /* Make the "default region number" fit in this function */
        current_region = mpu->reg_bcr.regions;
    }

    assert(current_region <= mpu->reg_bcr.regions);

    target_ulong page_addr = addr & PAGE_MASK;
    /*
     * Going through every region that has higher priority than
     * the current one.
     */
    for (uint8_t r = 0; r < current_region; ++r) {
        if (mpu->reg_base[r].valid &&
            page_addr == (mpu->reg_base[r].addr & PAGE_MASK)) {
            return false;
        }
    }
    /* No overlap with a higher priority region */
    return true;
}

/*
 * Update QEMU's TLB with region's permission.
 * One thing to remember is that if the region size
 * is smaller than TARGET_PAGE_SIZE, QEMU will always
 * consult tlb_fill() for any access to that region.
 * So there is no point in fine tunning TLB entry sizes
 * to reflect the real region size. On the other hand,
 * if the region size is big ( > TARGET_PAGE_SIZE), we
 * still go with TARGET_PAGE_SIZE, because it can be
 * memory demanding for host process.
 */
static void update_tlb_page(CPUARCState *env, uint8_t region,
                            target_ulong addr, int mmu_idx)
{
    CPUState *cs = env_cpu(env);
    /* By default, only add entry for 'addr' */
    target_ulong tlb_addr = addr;
    target_ulong tlb_size = 1;
    bool check_for_overlap = true;
    int prot = 0;

    if (region != MPU_DEFAULT_REGION_NR) {
        MPUPermReg *perm = &env->mpu.reg_perm[region];
        prot = mpu_permission_to_qemu(
                &perm->permission, is_user_mode(env));
        /*
         * If the region's size is big enough, we'll check for overlap.
         * Later if we find no overlap, then we add the permission for
         * the whole page to qemu's tlb.
         */
        check_for_overlap = (perm->size >= TARGET_PAGE_SIZE);
    }
    /* Default region */
    else {
        prot = mpu_permission_to_qemu(
                &env->mpu.reg_enable.permission, is_user_mode(env));
    }

    /*
     * If the region completely covers the 'page' that 'addr'
     * belongs to, _and_ is not overlapping with any other region
     * then add a 'page'wise entry.
     */
    if (check_for_overlap &&
        is_overlap_free(&env->mpu, addr, region)) {
        tlb_addr = addr & PAGE_MASK;
        tlb_size = TARGET_PAGE_SIZE;
    }

    tlb_set_page(cs, tlb_addr, tlb_addr, prot, mmu_idx, tlb_size);
    qemu_log_mask(CPU_LOG_MMU, "[MPU] TLB update: addr=0x%08x, "
            "prot=%c%c%c, mmu_idx=%u, page_size=%u\n", tlb_addr,
            (prot & PAGE_READ) ? 'r' : '-', (prot & PAGE_WRITE) ? 'w' : '-',
            (prot & PAGE_EXEC) ? 'x' : '-', mmu_idx, tlb_size);
}

/* The MPU entry point for any memory access */
int
arc_mpu_translate(CPUARCState *env, target_ulong addr,
                  MMUAccessType access, int mmu_idx)
{
    ARCMPU *mpu = &env->mpu;

    uint8_t region = get_matching_region(mpu, addr);
    const MPUPermissions *perms = get_permission(mpu, region);
    if (!allowed(access, is_user_mode(env), perms)) {
        set_exception(env, addr, region, access);
        return MPU_FAULT;
    }
    update_tlb_page(env, region, addr, mmu_idx);

    return MPU_SUCCESS;
}

/*-*-indent-tabs-mode:nil;tab-width:4;indent-line-function:'insert-tab'-*-*/
/* vim: set ts=4 sw=4 et: */
