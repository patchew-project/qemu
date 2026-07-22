/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 * Load/store for 512-bit atomic operations, generic version.
 */

#ifndef HOST_ATOMIC512_LDST_H
#define HOST_ATOMIC512_LDST_H

#define HAVE_ATOMIC512_LD 0
#define HAVE_ATOMIC512_ST 0

/* Fallback definitions that must be optimized away, or error.  */
Value64 QEMU_ERROR("unsupported atomic") atomic64_read(const void *s);
void QEMU_ERROR("unsupported atomic") atomic64_set(void *d, const Value64 *v);

#endif /* HOST_ATOMIC512_LDST_H */
