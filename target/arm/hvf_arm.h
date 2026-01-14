/*
 * QEMU Hypervisor.framework (HVF) support -- ARM specifics
 *
 * Copyright (c) 2021 Alexander Graf
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 *
 */

#ifndef QEMU_HVF_ARM_H
#define QEMU_HVF_ARM_H

#include "target/arm/cpu-qom.h"

#ifdef __MAC_OS_X_VERSION_MAX_ALLOWED
  #define HVF_SME2_AVAILABLE (__MAC_OS_X_VERSION_MAX_ALLOWED >= 150200)
  #include "system/hvf_int.h"
#else
  #define HVF_SME2_AVAILABLE 0
#endif


/**
 * hvf_arm_init_debug() - initialize guest debug capabilities
 *
 * Should be called only once before using guest debug capabilities.
 */
void hvf_arm_init_debug(void);

void hvf_arm_set_cpu_features_from_host(ARMCPU *cpu);

uint32_t hvf_arm_get_default_ipa_bit_size(void);
uint32_t hvf_arm_get_max_ipa_bit_size(void);

#if HVF_SME2_AVAILABLE
static inline bool hvf_arm_sme2_supported(void)
{
    if (__builtin_available(macOS 15.2, *)) {
        size_t svl_bytes;
        hv_return_t result = hv_sme_config_get_max_svl_bytes(&svl_bytes);
        if (result == HV_UNSUPPORTED) {
            return false;
        }
        assert_hvf_ok(result);
        return svl_bytes > 0;
    } else {
        return false;
    }
}

static inline uint32_t hvf_arm_sme2_get_svl(void)
{
    if (__builtin_available(macOS 15.2, *)) {
        size_t svl_bytes;
        hv_return_t result = hv_sme_config_get_max_svl_bytes(&svl_bytes);
        assert_hvf_ok(result);
        return svl_bytes;
    } else {
        abort();
    }
}
#else /* HVF_SME2_AVAILABLE */
static inline bool hvf_arm_sme2_supported(void)
{
    return false;
}
static inline uint32_t hvf_arm_sme2_get_svl(void)
{
    abort();
}
#endif /* HVF_SME2_AVAILABLE */

#endif
