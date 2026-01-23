/*
 * s390x cpacf
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#ifndef S390X_CPACF_H
#define S390X_CPACF_H

/* from crypto_sha512.c */
int cpacf_sha512(CPUS390XState *env, uintptr_t ra, uint64_t param_addr,
                 uint64_t *message_reg, uint64_t *len_reg, uint32_t type);

#endif
