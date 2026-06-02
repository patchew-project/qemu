/*
 * RISC-V CPU extension validation
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "cpu.h"
#include "internals.h"
#include "qapi/error.h"
#include "qemu/error-report.h"

/* Hash that stores user set extensions */
static GHashTable *multi_ext_user_opts;
static GHashTable *misa_ext_user_opts;

void riscv_cpu_ext_user_opts_init(void)
{
    g_clear_pointer(&multi_ext_user_opts, g_hash_table_unref);
    g_clear_pointer(&misa_ext_user_opts, g_hash_table_unref);

    multi_ext_user_opts = g_hash_table_new(NULL, g_direct_equal);
    misa_ext_user_opts = g_hash_table_new(NULL, g_direct_equal);
}

static bool cpu_cfg_ext_is_user_set(uint32_t ext_offset)
{
    if (!multi_ext_user_opts) {
        return false;
    }

    return g_hash_table_contains(multi_ext_user_opts,
                                 GUINT_TO_POINTER(ext_offset));
}

bool riscv_cpu_misa_ext_is_user_set(uint32_t misa_bit)
{
    if (!misa_ext_user_opts) {
        return false;
    }

    return g_hash_table_contains(misa_ext_user_opts,
                                 GUINT_TO_POINTER(misa_bit));
}

void riscv_cpu_cfg_ext_add_user_opt(uint32_t ext_offset, bool value)
{
    if (!multi_ext_user_opts) {
        riscv_cpu_ext_user_opts_init();
    }

    g_hash_table_insert(multi_ext_user_opts,
                        GUINT_TO_POINTER(ext_offset),
                        (gpointer)value);
}

void riscv_cpu_misa_ext_add_user_opt(uint32_t bit, bool value)
{
    if (!misa_ext_user_opts) {
        riscv_cpu_ext_user_opts_init();
    }

    g_hash_table_insert(misa_ext_user_opts,
                        GUINT_TO_POINTER(bit),
                        (gpointer)value);
}

void riscv_cpu_write_misa_bit(RISCVCPU *cpu, uint32_t bit,
                              bool enabled)
{
    CPURISCVState *env = &cpu->env;

    if (enabled) {
        env->misa_ext |= bit;
        env->misa_ext_mask |= bit;
    } else {
        env->misa_ext &= ~bit;
        env->misa_ext_mask &= ~bit;
    }
}

static int cpu_cfg_ext_get_min_version(uint32_t ext_offset)
{
    const RISCVIsaExtData *edata;

    for (edata = isa_edata_arr; edata && edata->name; edata++) {
        if (edata->ext_enable_offset != ext_offset) {
            continue;
        }

        return edata->min_version;
    }

    g_assert_not_reached();
}

const char *riscv_cpu_cfg_ext_get_name(uint32_t ext_offset)
{
    const RISCVIsaExtData *edata;

    for (edata = isa_edata_arr; edata->name != NULL; edata++) {
        if (edata->ext_enable_offset == ext_offset) {
            return edata->name;
        }
    }

    g_assert_not_reached();
}

void riscv_cpu_bump_multi_ext_priv_ver(CPURISCVState *env,
                                       uint32_t ext_offset)
{
    int ext_priv_ver;

    if (env->priv_ver == PRIV_VERSION_LATEST) {
        return;
    }

    ext_priv_ver = cpu_cfg_ext_get_min_version(ext_offset);

    if (env->priv_ver < ext_priv_ver) {
        /*
         * Note: the 'priv_spec' command line option, if present,
         * will take precedence over this priv_ver bump.
         */
        env->priv_ver = ext_priv_ver;
    }
}

void riscv_cpu_cfg_ext_auto_update(RISCVCPU *cpu, uint32_t ext_offset,
                                   bool value)
{
    CPURISCVState *env = &cpu->env;
    bool prev_val = isa_ext_is_enabled(cpu, ext_offset);
    int min_version;

    if (prev_val == value) {
        return;
    }

    if (cpu_cfg_ext_is_user_set(ext_offset)) {
        return;
    }

    if (value && env->priv_ver != PRIV_VERSION_LATEST) {
        /* Do not enable it if priv_ver is older than min_version */
        min_version = cpu_cfg_ext_get_min_version(ext_offset);
        if (env->priv_ver < min_version) {
            return;
        }
    }

    isa_ext_update_enabled(cpu, ext_offset, value);
}

void riscv_cpu_validate_misa_priv(CPURISCVState *env, Error **errp)
{
    if (riscv_has_ext(env, RVH) && env->priv_ver < PRIV_VERSION_1_12_0) {
        error_setg(errp, "H extension requires priv spec 1.12.0");
        return;
    }
}

static void riscv_cpu_validate_v(CPURISCVState *env, RISCVCPUConfig *cfg,
                                 Error **errp)
{
    uint32_t min_vlen;
    uint32_t vlen = cfg->vlenb << 3;

    if (riscv_has_ext(env, RVV)) {
        min_vlen = 128;
    } else if (cfg->ext_zve64x) {
        min_vlen = 64;
    } else if (cfg->ext_zve32x) {
        min_vlen = 32;
    } else {
        return;
    }

    if (vlen > RV_VLEN_MAX || vlen < min_vlen) {
        error_setg(errp,
                   "Vector extension implementation only supports VLEN "
                   "in the range [%d, %d]", min_vlen, RV_VLEN_MAX);
        return;
    }

    if (cfg->elen > 64 || cfg->elen < 8) {
        error_setg(errp,
                   "Vector extension implementation only supports ELEN "
                   "in the range [8, 64]");
        return;
    }

    if (vlen < cfg->elen) {
        error_setg(errp, "Vector extension implementation requires VLEN "
                         "to be greater than or equal to ELEN");
        return;
    }
}

static void riscv_cpu_disable_priv_spec_isa_exts(RISCVCPU *cpu)
{
    CPURISCVState *env = &cpu->env;
    const RISCVIsaExtData *edata;

    /* Force disable extensions if priv spec version does not match */
    for (edata = isa_edata_arr; edata && edata->name; edata++) {
        if (isa_ext_is_enabled(cpu, edata->ext_enable_offset) &&
            (env->priv_ver < edata->min_version)) {
            /*
             * These two extensions are always enabled as they were supported
             * by QEMU before they were added as extensions in the ISA.
             */
            if (!strcmp(edata->name, "zicntr") ||
                !strcmp(edata->name, "zihpm")) {
                continue;
            }

            /*
             * cpu.debug = true is marked as 'sdtrig', priv spec 1.12.
             * Skip this warning since existing CPUs with older priv
             * spec and debug = true will be impacted.
             */
            if (!strcmp(edata->name, "sdtrig")) {
                continue;
            }

            isa_ext_update_enabled(cpu, edata->ext_enable_offset, false);
#ifndef CONFIG_USER_ONLY
            warn_report("disabling %s extension for hart 0x%" PRIx64
                        " because privilege spec version does not match",
                        edata->name, env->mhartid);
#else
            warn_report("disabling %s extension because "
                        "privilege spec version does not match",
                        edata->name);
#endif
        }
    }
}

void riscv_cpu_update_cfg(RISCVCPU *cpu)
{
    if (cpu->env.priv_ver >= PRIV_VERSION_1_11_0) {
        cpu->cfg.has_priv_1_11 = true;
    }

    if (cpu->env.priv_ver >= PRIV_VERSION_1_12_0) {
        cpu->cfg.has_priv_1_12 = true;
    }

    if (cpu->env.priv_ver >= PRIV_VERSION_1_13_0) {
        cpu->cfg.has_priv_1_13 = true;
    }

    /* zic64b is 1.12 or later */
    cpu->cfg.ext_zic64b = cpu->cfg.cbom_blocksize == 64 &&
                          cpu->cfg.cbop_blocksize == 64 &&
                          cpu->cfg.cboz_blocksize == 64 &&
                          cpu->cfg.has_priv_1_12;

    cpu->cfg.ext_ssstateen = cpu->cfg.ext_smstateen;

    cpu->cfg.ext_sha = riscv_has_ext(&cpu->env, RVH) &&
                       cpu->cfg.ext_ssstateen;

    cpu->cfg.ext_ziccrse = cpu->cfg.has_priv_1_11;
}

static void riscv_cpu_validate_g(RISCVCPU *cpu)
{
    const char *warn_msg = "RVG mandates disabled extension %s";
    uint32_t g_misa_bits[] = {RVI, RVM, RVA, RVF, RVD};
    bool send_warn = riscv_cpu_misa_ext_is_user_set(RVG);

    for (int i = 0; i < ARRAY_SIZE(g_misa_bits); i++) {
        uint32_t bit = g_misa_bits[i];

        if (riscv_has_ext(&cpu->env, bit)) {
            continue;
        }

        if (!riscv_cpu_misa_ext_is_user_set(bit)) {
            riscv_cpu_write_misa_bit(cpu, bit, true);
            continue;
        }

        if (send_warn) {
            warn_report(warn_msg, riscv_get_misa_ext_name(bit));
        }
    }

    if (!cpu->cfg.ext_zicsr) {
        if (!cpu_cfg_ext_is_user_set(CPU_CFG_OFFSET(ext_zicsr))) {
            cpu->cfg.ext_zicsr = true;
        } else if (send_warn) {
            warn_report(warn_msg, "zicsr");
        }
    }

    if (!cpu->cfg.ext_zifencei) {
        if (!cpu_cfg_ext_is_user_set(CPU_CFG_OFFSET(ext_zifencei))) {
            cpu->cfg.ext_zifencei = true;
        } else if (send_warn) {
            warn_report(warn_msg, "zifencei");
        }
    }
}

static void riscv_cpu_validate_b(RISCVCPU *cpu)
{
    const char *warn_msg = "RVB mandates disabled extension %s";

    if (!cpu->cfg.ext_zba) {
        if (!cpu_cfg_ext_is_user_set(CPU_CFG_OFFSET(ext_zba))) {
            cpu->cfg.ext_zba = true;
        } else {
            warn_report(warn_msg, "zba");
        }
    }

    if (!cpu->cfg.ext_zbb) {
        if (!cpu_cfg_ext_is_user_set(CPU_CFG_OFFSET(ext_zbb))) {
            cpu->cfg.ext_zbb = true;
        } else {
            warn_report(warn_msg, "zbb");
        }
    }

    if (!cpu->cfg.ext_zbs) {
        if (!cpu_cfg_ext_is_user_set(CPU_CFG_OFFSET(ext_zbs))) {
            cpu->cfg.ext_zbs = true;
        } else {
            warn_report(warn_msg, "zbs");
        }
    }
}

/*
 * Check consistency between chosen extensions while setting
 * cpu->cfg accordingly.
 */
void riscv_cpu_validate_set_extensions(RISCVCPU *cpu, Error **errp)
{
    RISCVCPUClass *mcc = RISCV_CPU_GET_CLASS(cpu);
    CPURISCVState *env = &cpu->env;
    Error *local_err = NULL;

    if (riscv_has_ext(env, RVG)) {
        riscv_cpu_validate_g(cpu);
    }

    if (riscv_has_ext(env, RVB)) {
        riscv_cpu_validate_b(cpu);
    }

    if (riscv_has_ext(env, RVI) && riscv_has_ext(env, RVE)) {
        error_setg(errp,
                   "I and E extensions are incompatible");
        return;
    }

    if (!riscv_has_ext(env, RVI) && !riscv_has_ext(env, RVE)) {
        error_setg(errp,
                   "Either I or E extension must be set");
        return;
    }

    if (riscv_has_ext(env, RVS) && !riscv_has_ext(env, RVU)) {
        error_setg(errp,
                   "Setting S extension without U extension is illegal");
        return;
    }

    if (riscv_has_ext(env, RVH) && !riscv_has_ext(env, RVI)) {
        error_setg(errp,
                   "H depends on an I base integer ISA with 32 x registers");
        return;
    }

    if (riscv_has_ext(env, RVH) && !riscv_has_ext(env, RVS)) {
        error_setg(errp, "H extension implicitly requires S-mode");
        return;
    }

    if (riscv_has_ext(env, RVF) && !cpu->cfg.ext_zicsr) {
        error_setg(errp, "F extension requires Zicsr");
        return;
    }

    if ((cpu->cfg.ext_zacas) && !riscv_has_ext(env, RVA)) {
        error_setg(errp, "Zacas extension requires A extension");
        return;
    }

    if ((cpu->cfg.ext_zawrs) && !riscv_has_ext(env, RVA)) {
        error_setg(errp, "Zawrs extension requires A extension");
        return;
    }

    if (cpu->cfg.ext_zfa && !riscv_has_ext(env, RVF)) {
        error_setg(errp, "Zfa extension requires F extension");
        return;
    }

    if (cpu->cfg.ext_zfhmin && !riscv_has_ext(env, RVF)) {
        error_setg(errp, "Zfh/Zfhmin extensions require F extension");
        return;
    }

    if (cpu->cfg.ext_zfbfmin && !riscv_has_ext(env, RVF)) {
        error_setg(errp, "Zfbfmin extension depends on F extension");
        return;
    }

    if (riscv_has_ext(env, RVD) && !riscv_has_ext(env, RVF)) {
        error_setg(errp, "D extension requires F extension");
        return;
    }

    riscv_cpu_validate_v(env, &cpu->cfg, &local_err);
    if (local_err != NULL) {
        error_propagate(errp, local_err);
        return;
    }

    /* The Zve64d extension depends on the Zve64f extension */
    if (cpu->cfg.ext_zve64d) {
        if (!riscv_has_ext(env, RVD)) {
            error_setg(errp, "Zve64d/V extensions require D extension");
            return;
        }
    }

    /* The Zve32f extension depends on the Zve32x extension */
    if (cpu->cfg.ext_zve32f) {
        if (!riscv_has_ext(env, RVF)) {
            error_setg(errp, "Zve32f/Zve64f extensions require F extension");
            return;
        }
    }

    if (cpu->cfg.ext_zvfhmin && !cpu->cfg.ext_zve32f) {
        error_setg(errp, "Zvfh/Zvfhmin extensions require Zve32f extension");
        return;
    }

    if (cpu->cfg.ext_zvfh && !cpu->cfg.ext_zfhmin) {
        error_setg(errp, "Zvfh extensions requires Zfhmin extension");
        return;
    }

    if (cpu->cfg.ext_zvfbfmin && !cpu->cfg.ext_zve32f) {
        error_setg(errp, "Zvfbfmin extension depends on Zve32f extension");
        return;
    }

    if (cpu->cfg.ext_zvfbfwma && !cpu->cfg.ext_zvfbfmin) {
        error_setg(errp, "Zvfbfwma extension depends on Zvfbfmin extension");
        return;
    }

    if (cpu->cfg.ext_zvfbfa) {
        if (!cpu->cfg.ext_zve32f || !cpu->cfg.ext_zfbfmin) {
            error_setg(errp, "Zvfbfa extension requires Zve32f extension "
                             "and Zfbfmin extension");
            return;
        }
    }

    if ((cpu->cfg.ext_zdinx || cpu->cfg.ext_zhinxmin) && !cpu->cfg.ext_zfinx) {
        error_setg(errp, "Zdinx/Zhinx/Zhinxmin extensions require Zfinx");
        return;
    }

    if (cpu->cfg.ext_zfinx) {
        if (!cpu->cfg.ext_zicsr) {
            error_setg(errp, "Zfinx extension requires Zicsr");
            return;
        }
        if (riscv_has_ext(env, RVF)) {
            error_setg(errp,
                       "Zfinx cannot be supported together with F extension");
            return;
        }
    }

    if (cpu->cfg.ext_zcmop && !cpu->cfg.ext_zca) {
        error_setg(errp, "Zcmop extensions require Zca");
        return;
    }

    if (mcc->def->misa_mxl_max != MXL_RV32 && cpu->cfg.ext_zcf) {
        error_setg(errp, "Zcf extension is only relevant to RV32");
        return;
    }

    if (!riscv_has_ext(env, RVF) && cpu->cfg.ext_zcf) {
        error_setg(errp, "Zcf extension requires F extension");
        return;
    }

    if (!riscv_has_ext(env, RVD) && cpu->cfg.ext_zcd) {
        error_setg(errp, "Zcd extension requires D extension");
        return;
    }

    if ((cpu->cfg.ext_zcf || cpu->cfg.ext_zcd || cpu->cfg.ext_zcb ||
         cpu->cfg.ext_zcmp || cpu->cfg.ext_zcmt) && !cpu->cfg.ext_zca) {
        error_setg(errp, "Zcf/Zcd/Zcb/Zcmp/Zcmt extensions require Zca "
                         "extension");
        return;
    }

    if (cpu->cfg.ext_zcd && (cpu->cfg.ext_zcmp || cpu->cfg.ext_zcmt)) {
        error_setg(errp, "Zcmp/Zcmt extensions are incompatible with "
                         "Zcd extension");
        return;
    }

    if (cpu->cfg.ext_zcmt && !cpu->cfg.ext_zicsr) {
        error_setg(errp, "Zcmt extension requires Zicsr extension");
        return;
    }

    if ((cpu->cfg.ext_zvbb || cpu->cfg.ext_zvkb || cpu->cfg.ext_zvkg ||
         cpu->cfg.ext_zvkned || cpu->cfg.ext_zvknha || cpu->cfg.ext_zvksed ||
         cpu->cfg.ext_zvksh) && !cpu->cfg.ext_zve32x) {
        error_setg(errp,
                   "Vector crypto extensions require V or Zve* extensions");
        return;
    }

    if ((cpu->cfg.ext_zvbc || cpu->cfg.ext_zvknhb) && !cpu->cfg.ext_zve64x) {
        error_setg(
            errp,
            "Zvbc and Zvknhb extensions require V or Zve64x extensions");
        return;
    }

    if (cpu->cfg.ext_zicntr && !cpu->cfg.ext_zicsr) {
        if (cpu_cfg_ext_is_user_set(CPU_CFG_OFFSET(ext_zicntr))) {
            error_setg(errp, "zicntr requires zicsr");
            return;
        }
        cpu->cfg.ext_zicntr = false;
    }

    if (cpu->cfg.ext_zihpm && !cpu->cfg.ext_zicsr) {
        if (cpu_cfg_ext_is_user_set(CPU_CFG_OFFSET(ext_zihpm))) {
            error_setg(errp, "zihpm requires zicsr");
            return;
        }
        cpu->cfg.ext_zihpm = false;
    }

    if (cpu->cfg.ext_zicfiss) {
        if (!cpu->cfg.ext_zicsr) {
            error_setg(errp, "zicfiss extension requires zicsr extension");
            return;
        }
        if (!riscv_has_ext(env, RVA)) {
            error_setg(errp, "zicfiss extension requires A extension");
            return;
        }
        if (!riscv_has_ext(env, RVS)) {
            error_setg(errp, "zicfiss extension requires S");
            return;
        }
        if (!cpu->cfg.ext_zimop) {
            error_setg(errp, "zicfiss extension requires zimop extension");
            return;
        }
        if (cpu->cfg.ext_zca && !cpu->cfg.ext_zcmop) {
            error_setg(errp, "zicfiss with zca requires zcmop extension");
            return;
        }
    }

    if (!cpu->cfg.ext_zihpm) {
        cpu->cfg.pmu_mask = 0;
        cpu->pmu_avail_ctrs = 0;
    }

    if (cpu->cfg.ext_zclsd) {
        if (riscv_has_ext(env, RVC) && riscv_has_ext(env, RVF)) {
            error_setg(errp,
                    "Zclsd cannot be supported together with C and F extension");
            return;
        }
        if (cpu->cfg.ext_zcf) {
            error_setg(errp,
                    "Zclsd cannot be supported together with Zcf extension");
            return;
        }
    }

    if (cpu->cfg.ext_zicfilp && !cpu->cfg.ext_zicsr) {
        error_setg(errp, "zicfilp extension requires zicsr extension");
        return;
    }

    if (mcc->def->misa_mxl_max == MXL_RV32 && cpu->cfg.ext_svukte) {
        error_setg(errp, "svukte is not supported for RV32");
        return;
    }

    if ((cpu->cfg.ext_smctr || cpu->cfg.ext_ssctr) &&
        (!riscv_has_ext(env, RVS) || !cpu->cfg.ext_sscsrind)) {
        if (cpu_cfg_ext_is_user_set(CPU_CFG_OFFSET(ext_smctr)) ||
            cpu_cfg_ext_is_user_set(CPU_CFG_OFFSET(ext_ssctr))) {
            error_setg(errp, "Smctr and Ssctr require S-mode and Sscsrind");
            return;
        }
        cpu->cfg.ext_smctr = false;
        cpu->cfg.ext_ssctr = false;
    }

    if (cpu->cfg.ext_svrsw60t59b &&
        (!cpu->cfg.mmu || mcc->def->misa_mxl_max == MXL_RV32)) {
        error_setg(errp, "svrsw60t59b is not supported on RV32 and MMU-less platforms");
        return;
    }

    /*
     * Disable isa extensions based on priv spec after we
     * validated and set everything we need.
     */
    riscv_cpu_disable_priv_spec_isa_exts(cpu);
}
