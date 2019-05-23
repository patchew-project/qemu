/*
 * QTest TPM commont test code
 *
 * Copyright (c) 2018 IBM Corporation
 *
 * Authors:
 *   Stefan Berger <stefanb@linux.vnet.ibm.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

/* FIXME Does not pass make check-headers, yet! */

#ifndef TESTS_TPM_TESTS_H
#define TESTS_TPM_TESTS_H

#include "tpm-util.h"

void tpm_test_swtpm_test(const char *src_tpm_path, tx_func *tx,
                         const char *ifmodel);

void tpm_test_swtpm_migration_test(const char *src_tpm_path,
                                   const char *dst_tpm_path,
                                   const char *uri, tx_func *tx,
                                   const char *ifmodel);

#endif /* TESTS_TPM_TESTS_H */
