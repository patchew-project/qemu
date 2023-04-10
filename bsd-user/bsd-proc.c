/*
 *  BSD process related system call helpers
 *
 *  Copyright (c) 2013-14 Stacey D. Son
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
#include "qemu/osdep.h"

#include "qemu.h"
#include "qemu-bsd.h"
#include "signal-common.h"

void h2g_rusage(const struct rusage *rusage,
                struct target_freebsd_rusage *target_rusage)
{
    __put_user(rusage->ru_utime.tv_sec, &target_rusage->ru_utime.tv_sec);
    __put_user(rusage->ru_utime.tv_usec, &target_rusage->ru_utime.tv_usec);

    __put_user(rusage->ru_stime.tv_sec, &target_rusage->ru_stime.tv_sec);
    __put_user(rusage->ru_stime.tv_usec, &target_rusage->ru_stime.tv_usec);

    __put_user(rusage->ru_maxrss, &target_rusage->ru_maxrss);
    __put_user(rusage->ru_idrss, &target_rusage->ru_idrss);
    __put_user(rusage->ru_idrss, &target_rusage->ru_idrss);
    __put_user(rusage->ru_isrss, &target_rusage->ru_isrss);
    __put_user(rusage->ru_minflt, &target_rusage->ru_minflt);
    __put_user(rusage->ru_majflt, &target_rusage->ru_majflt);
    __put_user(rusage->ru_nswap, &target_rusage->ru_nswap);
    __put_user(rusage->ru_inblock, &target_rusage->ru_inblock);
    __put_user(rusage->ru_oublock, &target_rusage->ru_oublock);
    __put_user(rusage->ru_msgsnd, &target_rusage->ru_msgsnd);
    __put_user(rusage->ru_msgrcv, &target_rusage->ru_msgrcv);
    __put_user(rusage->ru_nsignals, &target_rusage->ru_nsignals);
    __put_user(rusage->ru_nvcsw, &target_rusage->ru_nvcsw);
    __put_user(rusage->ru_nivcsw, &target_rusage->ru_nivcsw);
}
