/*
 * TPM stubs
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */
#include "qemu/osdep.h"
#include "sysemu/tpm.h"
#include "sysemu/tpm_backend.h"
#include "qmp-commands.h"

int tpm_init(void)
{
    return 0;
}

void tpm_cleanup(void)
{
}

TPMInfoList *qmp_query_tpm(Error **errp)
{
    return NULL;
}

TpmTypeList *qmp_query_tpm_types(Error **errp)
{
    return NULL;
}

TpmModelList *qmp_query_tpm_models(Error **errp)
{
    return NULL;
}

void tpm_backend_reset(TPMBackend *s)
{
    return;
}

int tpm_backend_init(TPMBackend *s, TPMIf *tpmif, Error **errp)
{
    return 0;
}

TPMBackend *qemu_find_tpm_be(const char *id)
{
    return NULL;
}