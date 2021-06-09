/*
 * TPM stubs
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"
#include "qapi/qapi-commands-tpm.h"
#include "qapi/qmp/qerror.h"
#include "qapi/error.h"
#include "sysemu/tpm.h"
#include "hw/acpi/tpm.h"

int tpm_init(void)
{
    return 0;
}

void tpm_cleanup(void)
{
}

TPMInfoList *qmp_query_tpm(Error **errp)
{
    error_setg(errp, QERR_UNSUPPORTED);
    return NULL;
}

TpmTypeList *qmp_query_tpm_types(Error **errp)
{
    error_setg(errp, QERR_UNSUPPORTED);
    return NULL;
}

TpmModelList *qmp_query_tpm_models(Error **errp)
{
    error_setg(errp, QERR_UNSUPPORTED);
    return NULL;
}

void tpm_build_ppi_acpi(TPMIf *tpm, Aml *dev)
{
}
