/*
 * user-mmap.h: prototypes for linux-user guest binary loader
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#ifndef LINUX_USER_USER_MMAP_H
#define LINUX_USER_USER_MMAP_H

int target_mprotect(abi_ulong start, abi_ulong len, int prot);
abi_long target_mmap(abi_ulong start, abi_ulong len, int prot,
                     int flags, int fd, abi_ulong offset);
int target_munmap(abi_ulong start, abi_ulong len);
abi_long target_mremap(abi_ulong old_addr, abi_ulong old_size,
                       abi_ulong new_size, unsigned long flags,
                       abi_ulong new_addr);
abi_long target_madvise(abi_ulong start, abi_ulong len_in, int advice);
extern unsigned long last_brk;
extern abi_ulong mmap_next_start;
abi_ulong mmap_find_vma(abi_ulong, abi_ulong, abi_ulong);
void mmap_fork_start(void);
void mmap_fork_end(int child);
abi_long old_mmap_get_args(abi_long *arg1, abi_long *arg2, abi_long *arg3,
                           abi_long *arg4, abi_long *arg5, abi_long *arg6);

#if (defined(TARGET_I386) && defined(TARGET_ABI32)) || \
    (defined(TARGET_ARM) && defined(TARGET_ABI32)) || \
    defined(TARGET_M68K) || defined(TARGET_CRIS) || \
    defined(TARGET_MICROBLAZE) || defined(TARGET_S390X)
/* __ARCH_WANT_SYS_OLD_MMAP */
#define mmap_get_args old_mmap_get_args
#else
#define mmap_get_args(arg1, arg2, arg3, arg4, arg5, arg6) 0
#endif

#endif /* LINUX_USER_USER_MMAP_H */
