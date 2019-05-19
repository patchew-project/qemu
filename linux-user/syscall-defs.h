/*
 *  Linux syscall definitions
 *  Copyright (c) 2003 Fabrice Bellard
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

SYSCALL_DEF_FULL(brk, .impl = impl_brk,
                 .print_ret = print_syscall_ptr_ret,
                 .arg_type = { ARG_PTR });
SYSCALL_DEF(chdir, ARG_STR);
SYSCALL_DEF_ARGS(clone, ARG_CLONEFLAG, ARG_PTR, ARG_PTR, ARG_PTR, ARG_PTR);
SYSCALL_DEF(close, ARG_DEC);
#ifdef TARGET_NR_creat
SYSCALL_DEF(creat, ARG_STR, ARG_MODEFLAG);
#endif
SYSCALL_DEF(exit, ARG_DEC);
SYSCALL_DEF(execve, ARG_STR, ARG_PTR, ARG_PTR);
SYSCALL_DEF(execveat, ARG_ATDIRFD, ARG_STR, ARG_PTR, ARG_PTR, ARG_ATFLAG);
#ifdef TARGET_NR_fork
SYSCALL_DEF(fork);
#endif
#ifdef TARGET_NR_ipc
SYSCALL_DEF_ARGS(ipc, ARG_HEX, ARG_DEC, ARG_DEC, ARG_HEX, ARG_PTR, ARG_HEX);
#endif
#ifdef TARGET_NR_link
SYSCALL_DEF(link, ARG_STR, ARG_STR);
#endif
SYSCALL_DEF(linkat, ARG_ATDIRFD, ARG_STR, ARG_ATDIRFD, ARG_STR, ARG_ATFLAG);
SYSCALL_DEF(mlock, ARG_PTR, ARG_DEC);
SYSCALL_DEF(mlockall, ARG_HEX);
#ifdef TARGET_NR_mmap
SYSCALL_DEF_FULL(mmap, .impl = impl_mmap,
                 .args = args_mmap,
                 .print_ret = print_syscall_ptr_ret,
                 .arg_type = { ARG_PTR, ARG_DEC, ARG_MMAPPROT,
                               ARG_MMAPFLAG, ARG_DEC, ARG_DEC });
#endif
#ifdef TARGET_NR_mmap2
SYSCALL_DEF_FULL(mmap2, .impl = impl_mmap,
                 .args = args_mmap2,
                 .print_ret = print_syscall_ptr_ret,
                 .arg_type = { ARG_PTR, ARG_DEC, ARG_MMAPPROT,
                               ARG_MMAPFLAG, ARG_DEC, ARG_DEC64 });
#endif
SYSCALL_DEF(mprotect, ARG_PTR, ARG_DEC, ARG_MMAPPROT);
SYSCALL_DEF_FULL(mremap, .impl = impl_mremap,
                 .print_ret = print_syscall_ptr_ret,
                 .arg_type = { ARG_PTR, ARG_DEC, ARG_DEC, ARG_HEX, ARG_PTR });
#if !defined(SYSCALL_TABLE) || defined(TARGET_NR_msgctl)
SYSCALL_DEF(msgctl, ARG_DEC, ARG_DEC, ARG_PTR);
#endif
#if !defined(SYSCALL_TABLE) || defined(TARGET_NR_msgget)
SYSCALL_DEF(msgget, ARG_DEC, ARG_DEC);
#endif
#if !defined(SYSCALL_TABLE) || defined(TARGET_NR_msgrcv)
SYSCALL_DEF(msgrcv, ARG_DEC, ARG_PTR, ARG_DEC, ARG_DEC, ARG_HEX);
#endif
#if !defined(SYSCALL_TABLE) || defined(TARGET_NR_msgsnd)
SYSCALL_DEF(msgsnd, ARG_DEC, ARG_PTR, ARG_DEC, ARG_HEX);
#endif
SYSCALL_DEF(msync, ARG_PTR, ARG_DEC, ARG_HEX);
SYSCALL_DEF(munlock, ARG_PTR, ARG_DEC);
SYSCALL_DEF(munlockall);
SYSCALL_DEF(munmap, ARG_PTR, ARG_DEC);
SYSCALL_DEF(name_to_handle_at,
            ARG_ATDIRFD, ARG_STR, ARG_PTR, ARG_PTR, ARG_ATFLAG);
#ifdef TARGET_NR_open
SYSCALL_DEF(open, ARG_STR, ARG_OPENFLAG, ARG_MODEFLAG);
#endif
SYSCALL_DEF(openat, ARG_ATDIRFD, ARG_STR, ARG_OPENFLAG, ARG_MODEFLAG);
SYSCALL_DEF(open_by_handle_at, ARG_DEC, ARG_PTR, ARG_OPENFLAG);
SYSCALL_DEF_FULL(pread64, .impl = impl_pread64,
                 .args = args_pread64_pwrite64,
                 .arg_type = { ARG_DEC, ARG_PTR, ARG_DEC, ARG_DEC64 });
SYSCALL_DEF_FULL(pwrite64, .impl = impl_pwrite64,
                 .args = args_pread64_pwrite64,
                 .arg_type = { ARG_DEC, ARG_PTR, ARG_DEC, ARG_DEC64 });
SYSCALL_DEF_FULL(preadv, .impl = impl_preadv,
                 .args = args_preadv_pwritev,
                 .arg_type = { ARG_DEC, ARG_PTR, ARG_DEC, ARG_DEC64 });
SYSCALL_DEF_FULL(pwritev, .impl = impl_pwritev,
                 .args = args_preadv_pwritev,
                 .arg_type = { ARG_DEC, ARG_PTR, ARG_DEC, ARG_DEC64 });
SYSCALL_DEF(read, ARG_DEC, ARG_PTR, ARG_DEC);
#ifdef TARGET_NR_readlink
SYSCALL_DEF(readlink, ARG_STR, ARG_PTR, ARG_DEC);
#endif
#ifdef TARGET_NR_readlinkat
SYSCALL_DEF(readlinkat, ARG_ATDIRFD, ARG_STR, ARG_PTR, ARG_DEC);
#endif
SYSCALL_DEF(readv, ARG_DEC, ARG_PTR, ARG_DEC);
#ifdef TARGET_NR_rmdir
SYSCALL_DEF(rmdir, ARG_STR);
#endif
#if !defined(SYSCALL_TABLE) || defined(TARGET_NR_semctl)
SYSCALL_DEF(semctl, ARG_DEC, ARG_DEC, ARG_DEC, ARG_HEX);
#endif
#if !defined(SYSCALL_TABLE) || defined(TARGET_NR_semget)
SYSCALL_DEF(semget, ARG_DEC, ARG_DEC, ARG_HEX);
#endif
#if !defined(SYSCALL_TABLE) || defined(TARGET_NR_semop)
SYSCALL_DEF(semop, ARG_DEC, ARG_PTR, ARG_DEC);
#endif
#if !defined(SYSCALL_TABLE) || defined(TARGET_NR_shmat)
SYSCALL_DEF_FULL(shmat, .impl = impl_shmat,
                 .print_ret = print_syscall_ptr_ret,
                 .arg_type = { ARG_DEC, ARG_PTR, ARG_HEX });
#endif
#if !defined(SYSCALL_TABLE) || defined(TARGET_NR_shmctl)
SYSCALL_DEF(shmctl, ARG_DEC, ARG_DEC, ARG_PTR);
#endif
#if !defined(SYSCALL_TABLE) || defined(TARGET_NR_shmdt)
SYSCALL_DEF(shmdt, ARG_PTR);
#endif
#if !defined(SYSCALL_TABLE) || defined(TARGET_NR_shmget)
SYSCALL_DEF(shmget, ARG_DEC, ARG_DEC, ARG_HEX);
#endif
#ifdef TARGET_NR_unlink
SYSCALL_DEF(unlink, ARG_STR);
#endif
SYSCALL_DEF(unlinkat, ARG_ATDIRFD, ARG_STR, ARG_UNLINKATFLAG);
#ifdef TARGET_NR_vfork
/* Emulate vfork() with fork().  */
SYSCALL_DEF_FULL(vfork, .impl = impl_fork);
#endif
SYSCALL_DEF(wait4, ARG_DEC, ARG_PTR, ARG_HEX, ARG_PTR);
SYSCALL_DEF(waitid, ARG_HEX, ARG_DEC, ARG_PTR, ARG_HEX, ARG_PTR);
#ifdef TARGET_NR_waitpid
SYSCALL_DEF(waitpid, ARG_DEC, ARG_PTR, ARG_HEX);
#endif
SYSCALL_DEF(write, ARG_DEC, ARG_PTR, ARG_DEC);
SYSCALL_DEF(writev, ARG_DEC, ARG_PTR, ARG_DEC);
