/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * uefi vars device - AuthVariableLib
 */

#include "qemu/osdep.h"
#include "sysemu/dma.h"

#include "hw/uefi/var-service.h"

static const uint16_t name_pk[]  = { 'P', 'K',
                                     0 };
static const uint16_t name_setup_mode[]  = { 'S', 'e', 't', 'u', 'p',
                                             'M', 'o', 'd', 'e',
                                             0 };
static const uint16_t name_sb[]  = { 'S', 'e', 'c', 'u', 'r', 'e',
                                     'B', 'o', 'o', 't',
                                     0 };
static const uint16_t name_sb_enable[]  = { 'S', 'e', 'c', 'u', 'r', 'e',
                                            'B', 'o', 'o', 't',
                                            'E', 'n', 'a', 'b', 'l', 'e',
                                            0 };
static const uint16_t name_custom_mode[]  = { 'C', 'u', 's', 't', 'o', 'm',
                                              'M', 'o', 'd', 'e',
                                              0 };

/* AuthVariableLibInitialize */
void uefi_vars_auth_init(uefi_vars_state *uv)
{
    uefi_variable *pk_var, *sbe_var;;
    uint8_t platform_mode, sb, sbe, custom_mode;

    /* SetupMode */
    pk_var = uefi_vars_find_variable(uv, EfiGlobalVariable,
                                     name_pk, sizeof(name_pk));
    if (!pk_var) {
        platform_mode = SETUP_MODE;
    } else {
        platform_mode = USER_MODE;
    }
    uefi_vars_set_variable(uv, EfiGlobalVariable,
                           name_setup_mode, sizeof(name_setup_mode),
                           EFI_VARIABLE_BOOTSERVICE_ACCESS |
                           EFI_VARIABLE_RUNTIME_ACCESS,
                           &platform_mode, sizeof(platform_mode));

    /* TODO: SignatureSupport */

    /* SecureBootEnable */
    sbe = SECURE_BOOT_DISABLE;
    sbe_var = uefi_vars_find_variable(uv, EfiSecureBootEnableDisable,
                                      name_sb_enable, sizeof(name_sb_enable));
    if (sbe_var) {
        if (platform_mode == USER_MODE) {
            sbe = ((uint8_t*)sbe_var->data)[0];
        }
    } else if (platform_mode == USER_MODE) {
        sbe = SECURE_BOOT_ENABLE;
        uefi_vars_set_variable(uv, EfiSecureBootEnableDisable,
                               name_sb_enable, sizeof(name_sb_enable),
                               EFI_VARIABLE_NON_VOLATILE |
                               EFI_VARIABLE_BOOTSERVICE_ACCESS,
                               &sbe, sizeof(sbe));
    }

    /* SecureBoot */
    if ((sbe == SECURE_BOOT_ENABLE) && (platform_mode == USER_MODE)) {
        sb = SECURE_BOOT_MODE_ENABLE;
    } else {
        sb = SECURE_BOOT_MODE_DISABLE;
    }
    uefi_vars_set_variable(uv, EfiGlobalVariable,
                           name_sb, sizeof(name_sb),
                           EFI_VARIABLE_BOOTSERVICE_ACCESS |
                           EFI_VARIABLE_RUNTIME_ACCESS,
                           &sb, sizeof(sb));

    /* CustomMode */
    custom_mode = STANDARD_SECURE_BOOT_MODE;
    uefi_vars_set_variable(uv, EfiCustomModeEnable,
                           name_custom_mode, sizeof(name_custom_mode),
                           EFI_VARIABLE_NON_VOLATILE |
                           EFI_VARIABLE_BOOTSERVICE_ACCESS,
                           &custom_mode, sizeof(custom_mode));

    /* TODO: certdb */
    /* TODO: certdbv */
    /* TODO: VendorKeysNv */
    /* TODO: VendorKeys */
}
