/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * QAPI marshalling helpers for structures of the MCD API
 *
 * Copyright (c) 2025 Lauterbach GmbH
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#ifndef MCD_QAPI_H
#define MCD_QAPI_H

#include "mcd_api.h"
#include "mcd/mcd-qapi-types.h"

MCDAPIVersion *marshal_mcd_api_version(const mcd_api_version_st *api_version);

MCDImplVersionInfo *marshal_mcd_impl_version_info(
    const mcd_impl_version_info_st *impl_info);

MCDErrorInfo *marshal_mcd_error_info(const mcd_error_info_st *error_info);

mcd_api_version_st unmarshal_mcd_api_version(MCDAPIVersion *api_version);

#endif /* MCD_QAPI_H */
