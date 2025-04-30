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

MCDServerInfo *marshal_mcd_server_info(const mcd_server_info_st *server_info);

MCDCoreConInfo *marshal_mcd_core_con_info(const mcd_core_con_info_st *con_info);

MCDMemspace *marshal_mcd_memspace(const mcd_memspace_st *mem_space);

MCDRegisterGroup *marshal_mcd_register_group(
    const mcd_register_group_st *reg_group);

MCDAddr *marshal_mcd_addr(const mcd_addr_st *addr);

MCDRegisterInfo *marshal_mcd_register_info(
    const mcd_register_info_st *reg_info);

MCDCoreState *marshal_mcd_core_state(const mcd_core_state_st *state);

MCDTrigInfo *marshal_mcd_trig_info(const mcd_trig_info_st *trig_info);

MCDCtrigInfo *marshal_mcd_ctrig_info(const mcd_ctrig_info_st *trig_info);

MCDTrigSimpleCore *marshal_mcd_trig_simple_core(
    const mcd_trig_simple_core_st *trig_simple_core);

MCDTrigComplexCore *marshal_mcd_trig_complex_core(
    const mcd_trig_complex_core_st *trig_complex_core);

MCDTrigState *marshal_mcd_trig_state(const mcd_trig_state_st *trig_info);

MCDTrigSetState *marshal_mcd_trig_set_state(
    const mcd_trig_set_state_st *trig_state);

MCDTx *marshal_mcd_tx(const mcd_tx_st *tx);

MCDTxlist *marshal_mcd_txlist(const mcd_txlist_st *txlist);

MCDRstInfo *marshal_mcd_rst_info(const mcd_rst_info_st *rst_info);

mcd_api_version_st unmarshal_mcd_api_version(MCDAPIVersion *api_version);

mcd_core_con_info_st unmarshal_mcd_core_con_info(MCDCoreConInfo *con_info);

mcd_addr_st unmarshal_mcd_addr(MCDAddr *addr);

mcd_trig_simple_core_st unmarshal_mcd_trig_simple_core(
    MCDTrigSimpleCore *trig_simple_core);

mcd_trig_complex_core_st unmarshal_mcd_trig_complex_core(
    MCDTrigComplexCore *trig_complex_core);

mcd_tx_st unmarshal_mcd_tx(MCDTx *tx);

mcd_txlist_st unmarshal_mcd_txlist(MCDTxlist *txlist);

void free_mcd_tx(mcd_tx_st *tx);

void free_mcd_txlist(mcd_txlist_st *txlist);

#endif /* MCD_QAPI_H */
