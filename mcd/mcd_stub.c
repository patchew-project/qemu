/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * mcdstub - Conversion of MCD QAPI requests to MCD server function calls
 *
 * Copyright (c) 2025 Lauterbach GmbH
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"
#include "mcd_api.h"
#include "mcd_qapi.h"
#include "mcd/mcd-qapi-commands.h"

MCDInitializeResult *qmp_mcd_initialize(MCDAPIVersion *version_req,
                                        Error **errp)
{
    mcd_impl_version_info_st impl_info;
    MCDInitializeResult *result = g_malloc0(sizeof(*result));
    mcd_api_version_st version_req_unmarshalled =
        unmarshal_mcd_api_version(version_req);

    result->return_status = mcd_initialize_f(&version_req_unmarshalled,
                                             &impl_info);

    if (result->return_status == MCD_RET_ACT_NONE) {
        result->impl_info = marshal_mcd_impl_version_info(&impl_info);
    }

    return result;
}

void qmp_mcd_exit(Error **errp)
{
    mcd_exit_f();
}

MCDErrorInfo *qmp_mcd_qry_error_info(Error **errp)
{
    MCDErrorInfo *result;
    mcd_error_info_st error_info;
    mcd_qry_error_info_f(NULL, &error_info);
    result = marshal_mcd_error_info(&error_info);
    return result;
}
