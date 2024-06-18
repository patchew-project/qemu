/*
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#ifndef LIBQOS_CSR_H
#define LIBQOS_CSR_H

int qcsr_get_csr(QTestState *qts, uint64_t cpu,
        int csrno, uint64_t *val);

int qcsr_set_csr(QTestState *qts, uint64_t cpu,
        int csrno, uint64_t *val);


#endif /* LIBQOS_CSR_H */
