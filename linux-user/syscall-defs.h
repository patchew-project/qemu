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

#ifdef TARGET_NR_access
SYSCALL_DEF(access, ARG_STR, ARG_ACCESSFLAG);
#endif
SYSCALL_DEF(acct, ARG_STR);
#ifdef TARGET_NR_alarm
SYSCALL_DEF(alarm, ARG_DEC);
#endif
SYSCALL_DEF_FULL(brk, .impl = impl_brk,
                 .print_ret = print_syscall_ptr_ret,
                 .arg_type = { ARG_PTR });
SYSCALL_DEF(chdir, ARG_STR);
#ifdef TARGET_NR_chmod
SYSCALL_DEF(chmod, ARG_STR, ARG_MODEFLAG);
#endif
SYSCALL_DEF(chroot, ARG_STR);
SYSCALL_DEF_ARGS(clone, ARG_CLONEFLAG, ARG_PTR, ARG_PTR, ARG_PTR, ARG_PTR);
SYSCALL_DEF(close, ARG_DEC);
#ifdef TARGET_NR_creat
SYSCALL_DEF(creat, ARG_STR, ARG_MODEFLAG);
#endif
SYSCALL_DEF(dup, ARG_DEC);
#ifdef TARGET_NR_dup2
SYSCALL_DEF(dup2, ARG_DEC, ARG_DEC);
#endif
SYSCALL_DEF(dup3, ARG_DEC, ARG_DEC, ARG_OPENFLAG);
SYSCALL_DEF(exit, ARG_DEC);
SYSCALL_DEF(execve, ARG_STR, ARG_PTR, ARG_PTR);
SYSCALL_DEF(execveat, ARG_ATDIRFD, ARG_STR, ARG_PTR, ARG_PTR, ARG_ATFLAG);
SYSCALL_DEF(faccessat, ARG_ATDIRFD, ARG_STR, ARG_ACCESSFLAG);
SYSCALL_DEF(fchmod, ARG_DEC, ARG_MODEFLAG);
SYSCALL_DEF(fchmodat, ARG_ATDIRFD, ARG_STR, ARG_MODEFLAG);
#ifdef TARGET_NR_fcntl
SYSCALL_DEF_FULL(fcntl, .impl = impl_fcntl, .print = print_fcntl);
#endif
#if TARGET_ABI_BITS == 32
SYSCALL_DEF_FULL(fcntl64, .impl = impl_fcntl64, .print = print_fcntl64);
#endif
#ifdef TARGET_NR_futimesat
SYSCALL_DEF(futimesat, ARG_ATDIRFD, ARG_STR, ARG_PTR);
#endif
#ifdef TARGET_NR_fork
SYSCALL_DEF(fork);
#endif
#ifdef TARGET_NR_gethostname
SYSCALL_DEF(gethostname, ARG_PTR, ARG_DEC);
#endif
SYSCALL_DEF(getpgid, ARG_DEC);
#ifdef TARGET_NR_getpgrp
SYSCALL_DEF(getpgrp);
#endif
#ifdef TARGET_NR_getpid
SYSCALL_DEF(getpid);
#endif
#ifdef TARGET_NR_getppid
SYSCALL_DEF(getppid);
#endif
#ifdef TARGET_NR_getrlimit
SYSCALL_DEF(getrlimit, ARG_DEC, ARG_PTR);
#endif
SYSCALL_DEF(getrusage, ARG_DEC, ARG_PTR);
SYSCALL_DEF(getsid, ARG_DEC);
SYSCALL_DEF(gettimeofday, ARG_PTR);
#ifdef TARGET_NR_getxpid
SYSCALL_DEF(getxpid);
#endif
SYSCALL_DEF(ioctl, ARG_DEC, ARG_HEX);
#ifdef TARGET_NR_ipc
SYSCALL_DEF_ARGS(ipc, ARG_HEX, ARG_DEC, ARG_DEC, ARG_HEX, ARG_PTR, ARG_HEX);
#endif
SYSCALL_DEF(kill, ARG_DEC, ARG_SIGNAL);
#ifdef TARGET_NR_link
SYSCALL_DEF(link, ARG_STR, ARG_STR);
#endif
SYSCALL_DEF(linkat, ARG_ATDIRFD, ARG_STR, ARG_ATDIRFD, ARG_STR, ARG_ATFLAG);
#ifdef TARGET_NR_lseek
SYSCALL_DEF(lseek, ARG_DEC, ARG_DEC, ARG_LSEEKWHENCE);
#endif
#ifdef TARGET_NR_llseek
SYSCALL_DEF_ARGS(llseek, ARG_DEC, ARG_DEC, ARG_PTR, ARG_LSEEKWHENCE);
#endif
#ifdef TARGET_NR_mkdir
SYSCALL_DEF(mkdir, ARG_STR, ARG_MODEFLAG);
#endif
SYSCALL_DEF(mkdirat, ARG_ATDIRFD, ARG_STR, ARG_MODEFLAG);
#ifdef TARGET_NR_mknod
SYSCALL_DEF(mknod, ARG_STR, ARG_MODEFLAG, ARG_HEX);
#endif
SYSCALL_DEF(mknodat, ARG_ATDIRFD, ARG_STR, ARG_MODEFLAG, ARG_HEX);
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
SYSCALL_DEF(mount, ARG_STR, ARG_STR, ARG_STR, ARG_MOUNTFLAG, ARG_PTR);
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
#ifdef TARGET_NR__newselect
SYSCALL_DEF_FULL(_newselect, .impl = impl_select,
                 .arg_type = { ARG_DEC, ARG_PTR, ARG_PTR, ARG_PTR, ARG_PTR });
#endif
#ifdef TARGET_NR_nice
SYSCALL_DEF(nice, ARG_DEC);
#endif
#ifdef TARGET_NR_open
SYSCALL_DEF(open, ARG_STR, ARG_OPENFLAG, ARG_MODEFLAG);
#endif
SYSCALL_DEF(openat, ARG_ATDIRFD, ARG_STR, ARG_OPENFLAG, ARG_MODEFLAG);
SYSCALL_DEF(open_by_handle_at, ARG_DEC, ARG_PTR, ARG_OPENFLAG);
#ifdef TARGET_NR_pause
SYSCALL_DEF(pause);
#endif
#ifdef TARGET_NR_pipe
# if defined(TARGET_ALPHA) || defined(TARGET_MIPS) || \
     defined(TARGET_SH4) || defined(TARGET_SPARC)
/* ??? We have no way for strace to display the second returned fd.  */
SYSCALL_DEF(pipe);
# else
SYSCALL_DEF(pipe, ARG_PTR);
# endif
#endif
SYSCALL_DEF(pipe2, ARG_PTR, ARG_OPENFLAG);
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
SYSCALL_DEF(pselect6, ARG_DEC, ARG_PTR, ARG_PTR, ARG_PTR, ARG_PTR, ARG_PTR);
SYSCALL_DEF(read, ARG_DEC, ARG_PTR, ARG_DEC);
#ifdef TARGET_NR_readlink
SYSCALL_DEF(readlink, ARG_STR, ARG_PTR, ARG_DEC);
#endif
#ifdef TARGET_NR_readlinkat
SYSCALL_DEF(readlinkat, ARG_ATDIRFD, ARG_STR, ARG_PTR, ARG_DEC);
#endif
#ifdef TARGET_NR_rename
SYSCALL_DEF(rename, ARG_STR, ARG_STR);
#endif
#ifdef TARGET_NR_renameat
SYSCALL_DEF(renameat, ARG_ATDIRFD, ARG_STR, ARG_ATDIRFD, ARG_STR);
#endif
SYSCALL_DEF(renameat2, ARG_ATDIRFD, ARG_STR,
            ARG_ATDIRFD, ARG_STR, ARG_RENAMEFLAG);
SYSCALL_DEF(readv, ARG_DEC, ARG_PTR, ARG_DEC);
#ifdef TARGET_NR_rmdir
SYSCALL_DEF(rmdir, ARG_STR);
#endif
#if defined(TARGET_ALPHA)
SYSCALL_DEF(rt_sigaction, ARG_SIGNAL, ARG_PTR, ARG_PTR, ARG_DEC, ARG_PTR);
#elif defined(TARGET_SPARC)
SYSCALL_DEF(rt_sigaction, ARG_SIGNAL, ARG_PTR, ARG_PTR, ARG_PTR, ARG_DEC);
#else
SYSCALL_DEF(rt_sigaction, ARG_SIGNAL, ARG_PTR, ARG_PTR, ARG_DEC);
#endif
SYSCALL_DEF(rt_sigpending, ARG_PTR, ARG_DEC);
SYSCALL_DEF(rt_sigprocmask, ARG_SIGPROCMASKHOW, ARG_PTR, ARG_PTR, ARG_DEC);
SYSCALL_DEF(rt_sigqueueinfo, ARG_DEC, ARG_SIGNAL, ARG_PTR);
SYSCALL_DEF(rt_sigreturn);
SYSCALL_DEF(rt_sigsuspend, ARG_PTR, ARG_DEC);
SYSCALL_DEF(rt_sigtimedwait, ARG_PTR, ARG_PTR, ARG_PTR, ARG_DEC);
SYSCALL_DEF(rt_tgsigqueueinfo, ARG_DEC, ARG_DEC, ARG_SIGNAL, ARG_PTR);
#ifdef TARGET_NR_select
# if defined(TARGET_WANT_NI_OLD_SELECT)
SYSCALL_DEF_NOSYS(select);
# else
SYSCALL_DEF_ARGS(select, ARG_DEC, ARG_PTR, ARG_PTR, ARG_PTR, ARG_PTR);
# endif
#endif
#if !defined(SYSCALL_TABLE) || defined(TARGET_NR_semctl)
SYSCALL_DEF(semctl, ARG_DEC, ARG_DEC, ARG_DEC, ARG_HEX);
#endif
#if !defined(SYSCALL_TABLE) || defined(TARGET_NR_semget)
SYSCALL_DEF(semget, ARG_DEC, ARG_DEC, ARG_HEX);
#endif
SYSCALL_DEF(sethostname, ARG_STR);
SYSCALL_DEF(setpgid, ARG_DEC, ARG_DEC);
#ifdef TARGET_NR_setrlimit
SYSCALL_DEF(setrlimit, ARG_DEC, ARG_PTR);
#endif
SYSCALL_DEF(setsid);
SYSCALL_DEF(settimeofday, ARG_PTR, ARG_PTR);
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
#ifdef TARGET_NR_sigaction
SYSCALL_DEF(sigaction, ARG_SIGNAL, ARG_PTR, ARG_PTR);
#endif
#ifdef TARGET_NR_sigpending
SYSCALL_DEF(sigpending, ARG_PTR);
#endif
#if defined(TARGET_ALPHA)
SYSCALL_DEF(sigprocmask, ARG_SIGPROCMASKHOW, ARG_HEX);
#elif defined(TARGET_NR_sigprocmask)
SYSCALL_DEF(sigprocmask, ARG_SIGPROCMASKHOW, ARG_PTR, ARG_PTR);
#endif
#ifdef TARGET_NR_sigreturn
SYSCALL_DEF(sigreturn);
#endif
#if defined(TARGET_ALPHA)
SYSCALL_DEF(sigsuspend, ARG_HEX);
#elif defined(TARGET_NR_sigsuspend)
SYSCALL_DEF(sigsuspend, ARG_PTR);
#endif
#ifdef TARGET_NR_sgetmask
SYSCALL_DEF(sgetmask);
#endif
#ifdef TARGET_NR_ssetmask
SYSCALL_DEF(ssetmask, ARG_HEX);
#endif
#ifdef TARGET_NR_stime
SYSCALL_DEF(stime, ARG_PTR);
#endif
#ifdef TARGET_NR_symlink
SYSCALL_DEF(symlink, ARG_STR, ARG_STR);
#endif
SYSCALL_DEF(symlinkat, ARG_STR, ARG_ATDIRFD, ARG_STR);
SYSCALL_DEF(sync);
SYSCALL_DEF(syncfs, ARG_DEC);
#ifdef TARGET_NR_time
SYSCALL_DEF(time, ARG_PTR);
#endif
SYSCALL_DEF(times, ARG_PTR);
SYSCALL_DEF(umask, ARG_OCT);
#ifdef TARGET_NR_umount
SYSCALL_DEF(umount, ARG_STR);
#endif
SYSCALL_DEF(umount2, ARG_STR, ARG_UMOUNTFLAG);
#ifdef TARGET_NR_unlink
SYSCALL_DEF(unlink, ARG_STR);
#endif
SYSCALL_DEF(unlinkat, ARG_ATDIRFD, ARG_STR, ARG_UNLINKATFLAG);
#ifdef TARGET_NR_utime
SYSCALL_DEF(utime, ARG_STR, ARG_PTR);
#endif
#ifdef TARGET_NR_utimes
SYSCALL_DEF(utimes, ARG_STR, ARG_PTR);
#endif
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
