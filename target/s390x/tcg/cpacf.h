/*
 * s390x cpacf
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#ifndef S390X_CPACF_H
#define S390X_CPACF_H

/* from crypto_sha256.c */
int cpacf_sha256(CPUS390XState *env, uintptr_t ra, uint64_t param_addr,
                 uint64_t *message_reg, uint64_t *len_reg, uint32_t type);

/* from crypto_sha512.c */
int cpacf_sha512(CPUS390XState *env, uintptr_t ra, uint64_t param_addr,
                 uint64_t *message_reg, uint64_t *len_reg, uint32_t type);

/* from crypto_aes.c */
int cpacf_aes_ecb(CPUS390XState *env, uintptr_t ra, uint64_t param_addr,
                  uint64_t *dst_ptr, uint64_t *src_ptr, uint64_t *src_len,
                  uint32_t type, uint8_t fc, uint8_t mod);

#endif
