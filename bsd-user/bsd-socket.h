/*
 *  socket related system call shims
 *
 *  Copyright (c) 2013 Stacey D. Son
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

#ifndef BSD_SOCKET_H
#define BSD_SOCKET_H

#include <sys/types.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <netinet/in.h>

#include "qemu-bsd.h"

ssize_t safe_recvfrom(int s, void *buf, size_t len, int flags,
    struct sockaddr *restrict from, socklen_t *restrict fromlen);
ssize_t safe_sendto(int s, const void *buf, size_t len, int flags,
    const struct sockaddr *to, socklen_t tolen);
int safe_select(int nfds, fd_set *readfs, fd_set *writefds, fd_set *exceptfds,
    struct timeval *timeout);
int safe_pselect(int nfds, fd_set *restrict readfds,
    fd_set *restrict writefds, fd_set *restrict exceptfds,
    const struct timespec *restrict timeout,
    const sigset_t *restrict newsigmask);

/* bind(2) */
static inline abi_long do_bsd_bind(int sockfd, abi_ulong target_addr,
                                   socklen_t addrlen)
{
    abi_long ret;
    void *addr;

    if ((int)addrlen < 0) {
        return -TARGET_EINVAL;
    }

    addr = alloca(addrlen + 1);
    ret = target_to_host_sockaddr(addr, target_addr, addrlen);
    if (is_error(ret)) {
        return ret;
    }

    return get_errno(bind(sockfd, addr, addrlen));
}

/* connect(2) */
static inline abi_long do_bsd_connect(int sockfd, abi_ulong target_addr,
                                      socklen_t addrlen)
{
    abi_long ret;
    void *addr;

    if ((int)addrlen < 0) {
        return -TARGET_EINVAL;
    }
    addr = alloca(addrlen + 1);

    ret = target_to_host_sockaddr(addr, target_addr, addrlen);

    if (is_error(ret)) {
        return ret;
    }

    return get_errno(connect(sockfd, addr, addrlen));
}

/* accept(2) */
static inline abi_long do_bsd_accept(int fd, abi_ulong target_addr,
                                     abi_ulong target_addrlen_addr)
{
    socklen_t addrlen;
    void *addr;
    abi_long ret;

    if (target_addr == 0) {
        return get_errno(accept(fd, NULL, NULL));
    }
    /* return EINVAL if addrlen pointer is invalid */
    if (get_user_u32(addrlen, target_addrlen_addr)) {
        return -TARGET_EINVAL;
    }
    if ((int)addrlen < 0) {
        return -TARGET_EINVAL;
    }
    if (!access_ok(VERIFY_WRITE, target_addr, addrlen)) {
        return -TARGET_EINVAL;
    }
    addr = alloca(addrlen);

    ret = get_errno(accept(fd, addr, &addrlen));
    if (!is_error(ret)) {
        host_to_target_sockaddr(target_addr, addr, addrlen);
        if (put_user_u32(addrlen, target_addrlen_addr)) {
            ret = -TARGET_EFAULT;
        }
    }
    return ret;
}

/* getpeername(2) */
static inline abi_long do_bsd_getpeername(int fd, abi_ulong target_addr,
                                          abi_ulong target_addrlen_addr)
{
    socklen_t addrlen;
    void *addr;
    abi_long ret;

    if (get_user_u32(addrlen, target_addrlen_addr)) {
        return -TARGET_EFAULT;
    }
    if ((int)addrlen < 0) {
        return -TARGET_EINVAL;
    }
    if (!access_ok(VERIFY_WRITE, target_addr, addrlen)) {
        return -TARGET_EFAULT;
    }
    addr = alloca(addrlen);
    ret = get_errno(getpeername(fd, addr, &addrlen));
    if (!is_error(ret)) {
        host_to_target_sockaddr(target_addr, addr, addrlen);
        if (put_user_u32(addrlen, target_addrlen_addr)) {
            ret = -TARGET_EFAULT;
        }
    }
    return ret;
}

#endif /* BSD_SOCKET_H */
