/*
 * TPM stubs
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"
#include "sysemu/tpm.h"
#include "hw/acpi/tpm.h"

int tpm_init(void)
{
    return 0;
}

void tpm_cleanup(void)
{
}

void tpm_build_ppi_acpi(TPMIf *tpm, Aml *dev)
{
}
