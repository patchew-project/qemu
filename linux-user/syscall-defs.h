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

SYSCALL_DEF_ARGS(clone, ARG_CLONEFLAG, ARG_PTR, ARG_PTR, ARG_PTR, ARG_PTR);
SYSCALL_DEF(close, ARG_DEC);
#ifdef TARGET_NR_fork
SYSCALL_DEF(fork);
#endif
#ifdef TARGET_NR_getegid
SYSCALL_DEF(getegid);
#endif
#ifdef TARGET_NR_getegid32
SYSCALL_DEF(getegid32);
#endif
#ifdef TARGET_NR_geteuid
SYSCALL_DEF(geteuid);
#endif
#ifdef TARGET_NR_geteuid32
SYSCALL_DEF(geteuid32);
#endif
#ifdef TARGET_NR_getgid
SYSCALL_DEF(getgid);
#endif
#ifdef TARGET_NR_getgid32
SYSCALL_DEF(getgid32);
#endif
SYSCALL_DEF(getgroups, ARG_DEC, ARG_PTR);
#ifdef TARGET_NR_getgroups32
SYSCALL_DEF(getgroups32, ARG_DEC, ARG_PTR);
#endif
#ifdef TARGET_NR_getresgid
SYSCALL_DEF(getresgid, ARG_PTR, ARG_PTR, ARG_PTR);
#endif
#ifdef TARGET_NR_getresgid32
SYSCALL_DEF(getresgid32, ARG_PTR, ARG_PTR, ARG_PTR);
#endif
#ifdef TARGET_NR_getresuid
SYSCALL_DEF(getresuid, ARG_PTR, ARG_PTR, ARG_PTR);
#endif
#ifdef TARGET_NR_getresuid32
SYSCALL_DEF(getresuid32, ARG_PTR, ARG_PTR, ARG_PTR);
#endif
#ifdef TARGET_NR_getpgrp
SYSCALL_DEF(getpgrp);
#endif
#ifdef TARGET_NR_getpid
SYSCALL_DEF(getpid);
#endif
#ifdef TARGET_NR_getppid
SYSCALL_DEF(getppid);
#endif
SYSCALL_DEF(gettid);
#ifdef TARGET_NR_getuid
SYSCALL_DEF(getuid);
#endif
#ifdef TARGET_NR_getuid32
SYSCALL_DEF(getuid32);
#endif
#ifdef TARGET_NR_getxgid
SYSCALL_DEF(getxgid);
#endif
#ifdef TARGET_NR_getxpid
SYSCALL_DEF(getxpid);
#endif
#ifdef TARGET_NR_getxuid
SYSCALL_DEF(getxuid);
#endif
#ifdef TARGET_NR_get_thread_area
# if defined(TARGET_I386) && defined(TARGET_ABI32)
SYSCALL_DEF_FULL(get_thread_area, .impl = impl_get_thread_area,
                 .print_ret = print_syscall_ptr_ret,
                 .arg_type = { ARG_PTR });
# else
SYSCALL_DEF_FULL(get_thread_area, .impl = impl_get_thread_area,
                 .print_ret = print_syscall_ptr_ret);
# endif
#endif
#ifdef TARGET_NR_ipc
SYSCALL_DEF_ARGS(ipc, ARG_HEX, ARG_DEC, ARG_DEC, ARG_HEX, ARG_PTR, ARG_HEX);
#endif
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
#ifdef TARGET_NR_open
SYSCALL_DEF(open, ARG_STR, ARG_OPENFLAG, ARG_MODEFLAG);
#endif
SYSCALL_DEF(openat, ARG_ATDIRFD, ARG_STR, ARG_OPENFLAG, ARG_MODEFLAG);
SYSCALL_DEF(name_to_handle_at,
            ARG_ATDIRFD, ARG_STR, ARG_PTR, ARG_PTR, ARG_ATFLAG);
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
#if !defined(SYSCALL_TABLE) || defined(TARGET_NR_semctl)
SYSCALL_DEF(semctl, ARG_DEC, ARG_DEC, ARG_DEC, ARG_HEX);
#endif
#if !defined(SYSCALL_TABLE) || defined(TARGET_NR_semget)
SYSCALL_DEF(semget, ARG_DEC, ARG_DEC, ARG_HEX);
#endif
#if !defined(SYSCALL_TABLE) || defined(TARGET_NR_semop)
SYSCALL_DEF(semop, ARG_DEC, ARG_PTR, ARG_DEC);
#endif
SYSCALL_DEF(setfsgid, ARG_DEC);
#ifdef TARGET_NR_setfsgid32
SYSCALL_DEF(setfsgid32, ARG_DEC);
#endif
SYSCALL_DEF(setfsuid, ARG_DEC);
#ifdef TARGET_NR_setfsuid32
SYSCALL_DEF(setfsuid32, ARG_DEC);
#endif
SYSCALL_DEF(setgid, ARG_DEC);
#ifdef TARGET_NR_setgid32
SYSCALL_DEF(setgid32, ARG_DEC);
#endif
SYSCALL_DEF(setgroups, ARG_DEC, ARG_PTR);
#ifdef TARGET_NR_setgroups32
SYSCALL_DEF(setgroups32, ARG_DEC, ARG_PTR);
#endif
SYSCALL_DEF(setregid, ARG_DEC, ARG_DEC);
#ifdef TARGET_NR_setregid32
SYSCALL_DEF(setregid32, ARG_DEC, ARG_DEC);
#endif
#ifdef TARGET_NR_setresgid
SYSCALL_DEF(setresgid, ARG_DEC, ARG_DEC, ARG_DEC);
#endif
#ifdef TARGET_NR_setresgid32
SYSCALL_DEF(setresgid32, ARG_DEC, ARG_DEC, ARG_DEC);
#endif
#ifdef TARGET_NR_setresuid
SYSCALL_DEF(setresuid, ARG_DEC, ARG_DEC, ARG_DEC);
#endif
#ifdef TARGET_NR_setresuid32
SYSCALL_DEF(setresuid32, ARG_DEC, ARG_DEC, ARG_DEC);
#endif
SYSCALL_DEF(setreuid, ARG_DEC, ARG_DEC);
#ifdef TARGET_NR_setreuid32
SYSCALL_DEF(setreuid32, ARG_DEC, ARG_DEC);
#endif
SYSCALL_DEF(setsid);
SYSCALL_DEF(setuid, ARG_DEC);
#ifdef TARGET_NR_setuid32
SYSCALL_DEF(setuid32, ARG_DEC);
#endif
#ifdef TARGET_NR_set_thread_area
SYSCALL_DEF(set_thread_area, ARG_PTR);
#endif
SYSCALL_DEF(set_tid_address, ARG_PTR);
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
SYSCALL_DEF(write, ARG_DEC, ARG_PTR, ARG_DEC);
SYSCALL_DEF(writev, ARG_DEC, ARG_PTR, ARG_DEC);
#ifdef TARGET_NR_vfork
SYSCALL_DEF(vfork);
#endif
