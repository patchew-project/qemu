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
