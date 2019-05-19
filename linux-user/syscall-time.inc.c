/*
 *  Linux time related syscalls
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

SYSCALL_IMPL(gettimeofday)
{
    struct timeval tv;
    abi_long ret = get_errno(gettimeofday(&tv, NULL));

    if (!is_error(ret) && copy_to_user_timeval(arg1, &tv)) {
        return -TARGET_EFAULT;
    }
    return ret;
}

SYSCALL_IMPL(settimeofday)
{
    struct timeval tv, *ptv = NULL;
    struct timezone tz, *ptz = NULL;

    if (arg1) {
        if (copy_from_user_timeval(&tv, arg1)) {
            return -TARGET_EFAULT;
        }
        ptv = &tv;
    }

    if (arg2) {
        if (copy_from_user_timezone(&tz, arg2)) {
            return -TARGET_EFAULT;
        }
        ptz = &tz;
    }

    return get_errno(settimeofday(ptv, ptz));
}

#ifdef TARGET_NR_stime
SYSCALL_IMPL(stime)
{
    time_t host_time;

    if (get_user_sal(host_time, arg1)) {
        return -TARGET_EFAULT;
    }
    return get_errno(stime(&host_time));
}
#endif

#ifdef TARGET_NR_time
SYSCALL_IMPL(time)
{
    time_t host_time;
    abi_long ret = get_errno(time(&host_time));

    if (!is_error(ret)
        && arg1
        && put_user_sal(host_time, arg1)) {
        return -TARGET_EFAULT;
    }
    return ret;
}
#endif
