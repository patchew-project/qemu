/*
 * ARM Processor error injection
 *
 * Copyright(C) 2024 Huawei LTD.
 *
 * This code is licensed under the GPL version 2 or later. See the
 * COPYING file in the top-level directory.
 *
 */

#include "qemu/osdep.h"
#include "qapi/error.h"
#include "hw/boards.h"
#include "hw/acpi/ghes.h"
#include "cpu.h"

#define ACPI_GHES_ARM_CPER_CTX_DEFAULT_NREGS 74

/* Handle ARM Processor Error Information (PEI) */
static const ArmProcessorErrorInformationList *default_pei = { 0 };

static ArmPEI *qmp_arm_pei(uint16_t *err_info_num,
              bool has_error,
              ArmProcessorErrorInformationList const *error_list)
{
    ArmProcessorErrorInformationList const *next;
    ArmPeiValidationBitsList const *validation_list;
    ArmPEI *pei = NULL;
    uint16_t i;

    if (!has_error) {
        error_list = default_pei;
    }

    *err_info_num = 0;

    for (next = error_list; next; next = next->next) {
        (*err_info_num)++;

        if (*err_info_num >= 255) {
            break;
        }
    }

    pei = g_new0(ArmPEI, (*err_info_num));

    for (next = error_list, i = 0;
                i < *err_info_num; i++, next = next->next) {
        ArmProcessorErrorTypeList *type_list = next->value->type;
        uint16_t pei_validation = 0;
        uint8_t flags = 0;
        uint8_t type = 0;

        if (next->value->has_validation) {
            validation_list = next->value->validation;

            while (validation_list) {
                pei_validation |= BIT(next->value->validation->value);
                validation_list = validation_list->next;
            }
        }

        /*
         * According with UEFI 2.9A errata, the meaning of this field is
         * given by the following bitmap:
         *
         *   +-----|---------------------------+
         *   | Bit | Meaning                   |
         *   +=====+===========================+
         *   |  1  | Cache Error               |
         *   |  2  | TLB Error                 |
         *   |  3  | Bus Error                 |
         *   |  4  | Micro-architectural Error |
         *   +-----|---------------------------+
         *
         *   All other values are reserved.
         *
         * As bit 0 is reserved, QAPI ArmProcessorErrorType starts from bit 1.
         */
        while (type_list) {
            type |= BIT(type_list->value + 1);
            type_list = type_list->next;
        }
        if (!has_error) {
            type = BIT(ARM_PROCESSOR_ERROR_TYPE_CACHE_ERROR);
        }
        pei[i].type = type;

        if (next->value->has_flags) {
            ArmProcessorFlagsList *flags_list = next->value->flags;

            while (flags_list) {
                flags |= BIT(flags_list->value);
                flags_list = flags_list->next;
            }
        } else {
            flags = BIT(ARM_PROCESSOR_FLAGS_FIRST_ERROR_CAP) |
                    BIT(ARM_PROCESSOR_FLAGS_PROPAGATED);
        }
        pei[i].flags = flags;

        if (next->value->has_multiple_error) {
            pei[i].multiple_error = next->value->multiple_error;
            pei_validation |= BIT(ARM_PEI_VALIDATION_BITS_MULTIPLE_ERROR_VALID);
        }

        if (next->value->has_error_info) {
            pei[i].error_info = next->value->error_info;
        } else {
            switch (type) {
            case BIT(ARM_PROCESSOR_ERROR_TYPE_CACHE_ERROR):
                pei[i].error_info = 0x0091000F;
                break;
            case BIT(ARM_PROCESSOR_ERROR_TYPE_TLB_ERROR):
                pei[i].error_info = 0x0054007F;
                break;
            case BIT(ARM_PROCESSOR_ERROR_TYPE_BUS_ERROR):
                pei[i].error_info = 0x80D6460FFF;
                break;
            case BIT(ARM_PROCESSOR_ERROR_TYPE_MICRO_ARCH_ERROR):
                pei[i].error_info = 0x78DA03FF;
                break;
            default:
                /*
                 * UEFI 2.9A/2.10 doesn't define how this should be filled
                 * when multiple types are there. So, set default to zero,
                 * causing it to be removed from validation bits.
                 */
                pei[i].error_info = 0;
            }
        }

        if (next->value->has_virt_addr) {
            pei[i].virt_addr = next->value->virt_addr;
            pei_validation |= BIT(ARM_PEI_VALIDATION_BITS_VIRT_ADDR_VALID);
        }

        if (next->value->has_phy_addr) {
            pei[i].phy_addr = next->value->phy_addr;
            pei_validation |= BIT(ARM_PEI_VALIDATION_BITS_PHY_ADDR_VALID);
        }

        if (!next->value->has_validation) {
            if (pei[i].flags) {
                pei_validation |= BIT(ARM_PEI_VALIDATION_BITS_FLAGS_VALID);
            }
            if (pei[i].error_info) {
                pei_validation |= BIT(ARM_PEI_VALIDATION_BITS_ERROR_INFO_VALID);
            }
            if (next->value->has_virt_addr) {
                pei_validation |= BIT(ARM_PEI_VALIDATION_BITS_VIRT_ADDR_VALID);
            }

            if (next->value->has_phy_addr) {
                pei_validation |= BIT(ARM_PEI_VALIDATION_BITS_PHY_ADDR_VALID);
            }
        }

        pei[i].validation = pei_validation;
    }

    return pei;
}

/*
 * UEFI 2.10 default context register type (See UEFI 2.10 table N.21 for more)
 */
#define CONTEXT_AARCH32_EL1   1
#define CONTEXT_AARCH64_EL1   5

static int get_default_context_type(void)
{
    ARMCPU *cpu = ARM_CPU(qemu_get_cpu(0));
    bool aarch64;

    aarch64 = object_property_get_bool(OBJECT(cpu), "aarch64", NULL);

    if (aarch64) {
        return CONTEXT_AARCH64_EL1;
    }
    return CONTEXT_AARCH32_EL1;
}

/* Handle ARM Context */
static ArmContext *qmp_arm_context(uint16_t *context_info_num,
                                   uint32_t *context_length,
                                   bool has_context,
                                   ArmProcessorContextList const *context_list)
{
    ArmProcessorContextList const *next;
    ArmContext *context = NULL;
    uint16_t i, j, num, default_type;

    default_type = get_default_context_type();

    if (!has_context) {
        *context_info_num = 0;
        *context_length = 0;

        return NULL;
    }

    /* Calculate sizes */
    num = 0;
    for (next = context_list; next; next = next->next) {
        uint32_t n_regs = 0;

        if (next->value->has_q_register) {
            uint64List *reg = next->value->q_register;

            while (reg) {
                n_regs++;
                reg = reg->next;
            }

            if (next->value->has_minimal_size &&
                                        next->value->minimal_size < n_regs) {
                n_regs = next->value->minimal_size;
            }
        } else if (!next->value->has_minimal_size) {
            n_regs = ACPI_GHES_ARM_CPER_CTX_DEFAULT_NREGS;
        }

        if (!n_regs) {
            next->value->minimal_size = 0;
        } else {
            next->value->minimal_size = (n_regs + 1) % 0xfffe;
        }

        num++;
        if (num >= 65535) {
            break;
        }
    }

    context = g_new0(ArmContext, num);

    /* Fill context data */

    *context_length = 0;
    *context_info_num = 0;

    next = context_list;
    for (i = 0; i < num; i++, next = next->next) {
        if (!next->value->minimal_size) {
            continue;
        }

        if (next->value->has_type) {
            context[*context_info_num].type = next->value->type;
        } else {
            context[*context_info_num].type = default_type;
        }
        context[*context_info_num].size = next->value->minimal_size;
        context[*context_info_num].array = g_malloc0(context[*context_info_num].size * 8);

        (*context_info_num)++;

        /* length = 64 bits * (size of the reg array + context type) */
        *context_length += (context->size + 1) * 8;

        if (!next->value->has_q_register) {
            *context->array = 0xDEADBEEF;
        } else {
            uint64_t *pos = context->array;
            uint64List *reg = next->value->q_register;

            for (j = 0; j < context->size; j++) {
                if (!reg) {
                    break;
                }

                *(pos++) = reg->value;
                reg = reg->next;
            }
        }
    }

    if (!*context_info_num) {
        g_free(context);
        return NULL;
    }

    return context;
}

static uint8_t *qmp_arm_vendor(uint32_t *vendor_num, bool has_vendor_specific,
                               uint8List const *vendor_specific_list)
{
    uint8List const *next = vendor_specific_list;
    uint8_t *vendor = NULL, *p;

    if (!has_vendor_specific) {
        return NULL;
    }

    *vendor_num = 0;

    while (next) {
        next = next->next;
        (*vendor_num)++;
    }

    vendor = g_malloc(*vendor_num);

    p = vendor;
    next = vendor_specific_list;
    while (next) {
        *p = next->value;
        next = next->next;
        p++;
    }

    return vendor;
}

/* For ARM processor errors */
void qmp_arm_inject_error(bool has_validation,
                    ArmProcessorValidationBitsList *validation_list,
                    bool has_affinity_level,
                    uint8_t affinity_level,
                    bool has_mpidr_el1,
                    uint64_t mpidr_el1,
                    bool has_midr_el1,
                    uint64_t midr_el1,
                    bool has_running_state,
                    ArmProcessorRunningStateList *running_state_list,
                    bool has_psci_state,
                    uint32_t psci_state,
                    bool has_context, ArmProcessorContextList *context_list,
                    bool has_vendor_specific, uint8List *vendor_specific_list,
                    bool has_error,
                    ArmProcessorErrorInformationList *error_list,
                    Error **errp)
{
    MachineState *machine = MACHINE(qdev_get_machine());
    MachineClass *mc = MACHINE_GET_CLASS(machine);
    ARMCPU *armcpu = ARM_CPU(qemu_get_cpu(0));
    uint32_t running_state = 0;
    uint16_t validation = 0;
    ArmError error;
    uint16_t i;

    /* Handle UEFI 2.0 N.16 specific fields, setting defaults when needed */

    if (!has_midr_el1) {
        mpidr_el1 = armcpu->midr;
    }

    if (!has_mpidr_el1) {
        mpidr_el1 = armcpu->mpidr;
    }

    if (has_running_state) {
        while (running_state_list) {
            running_state |= BIT(running_state_list->value);
            running_state_list = running_state_list;
        }

        if (running_state) {
            error.psci_state = 0;
        }
    }

    if (has_validation) {
        while (validation_list) {
            validation |= BIT(validation_list->value);
            validation_list = validation_list->next;
        }
    } else {
        if (has_vendor_specific) {
            validation |= BIT(ARM_PROCESSOR_VALIDATION_BITS_VENDOR_SPECIFIC_VALID);
        }

        if (has_affinity_level) {
            validation |= BIT(ARM_PROCESSOR_VALIDATION_BITS_AFFINITY_VALID);
        }

        if (mpidr_el1) {
            validation = BIT(ARM_PROCESSOR_VALIDATION_BITS_MPIDR_VALID);
        }

        if (!has_running_state) {
            validation |= BIT(ARM_PROCESSOR_VALIDATION_BITS_RUNNING_STATE_VALID);
        }
    }

    /* Fill an error record */

    error.validation = validation;
    error.affinity_level = affinity_level;
    error.mpidr_el1 = mpidr_el1;
    error.midr_el1 = midr_el1;
    error.running_state = running_state;
    error.psci_state = psci_state;

    error.pei = qmp_arm_pei(&error.err_info_num, has_error, error_list);
    error.context = qmp_arm_context(&error.context_info_num,
                                    &error.context_length,
                                    has_context, context_list);
    error.vendor = qmp_arm_vendor(&error.vendor_num, has_vendor_specific,
                                  vendor_specific_list);

    ghes_record_arm_errors(error, ACPI_GHES_NOTIFY_GPIO);

    if (error.context) {
        for (i = 0; i < error.context_info_num; i++) {
            g_free(error.context[i].array);
        }
    }
    g_free(error.context);
    g_free(error.pei);
    g_free(error.vendor);

    if (mc->set_error) {
        mc->set_error();
    }

    return;
}
