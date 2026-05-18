/*
 * FreeBSD socket related system call shims
 *
 * Copyright (c) 2013-2014 Stacey D. Son
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#ifndef BSD_USER_FREEBSD_OS_SOCKET_H
#define BSD_USER_FREEBSD_OS_SOCKET_H

#include <sys/types.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <netinet/in.h>

#include "qemu-os.h"

ssize_t safe_recvmsg(int s, struct msghdr *msg, int flags);
ssize_t safe_sendmsg(int s, const struct msghdr *msg, int flags);

/* do_sendrecvmsg_locked() Must return target values and target errnos. */
static abi_long do_sendrecvmsg_locked(int fd, struct target_msghdr *msgp,
                                      int flags, int send)
{
    abi_long ret, len;
    struct msghdr msg;
    abi_ulong count;
    struct iovec *vec;
    abi_ulong target_vec;

    if (msgp->msg_name) {
        msg.msg_namelen = tswap32(msgp->msg_namelen);
        msg.msg_name = alloca(msg.msg_namelen + 1);
        ret = target_to_host_sockaddr(msg.msg_name,
                                      tswapal(msgp->msg_name),
                                      msg.msg_namelen);
        if (ret == -TARGET_EFAULT) {
            /*
             * For connected sockets msg_name and msg_namelen must be ignored,
             * so returning EFAULT immediately is wrong.  Instead, pass a bad
             * msg_name to the host kernel, and let it decide whether to return
             * EFAULT or not.
             */
            msg.msg_name = (void *)-1;
        } else if (ret) {
            goto out2;
        }
    } else {
        msg.msg_name = NULL;
        msg.msg_namelen = 0;
    }
    msg.msg_controllen = 2 * tswap32(msgp->msg_controllen);
    if (msgp->msg_control) {
        msg.msg_control = alloca(msg.msg_controllen);
        memset(msg.msg_control, 0, msg.msg_controllen);
    } else {
        msg.msg_control = NULL;
    }

    msg.msg_flags = tswap32(msgp->msg_flags);

    count = tswap32(msgp->msg_iovlen);
    target_vec = tswapal(msgp->msg_iov);

    if (count > IOV_MAX) {
        /*
         * sendrcvmsg returns a different errno for this condition than
         * readv/writev, so we must catch it here before lock_iovec() does.
         */
        ret = -TARGET_EMSGSIZE;
        goto out2;
    }

    vec = lock_iovec(send ? VERIFY_READ : VERIFY_WRITE,
                     target_vec, count, send);
    if (vec == NULL) {
        ret = -host_to_target_errno(errno);
        goto out2;
    }
    msg.msg_iovlen = count;
    msg.msg_iov = vec;

    if (send) {
        if (msg.msg_control != NULL) {
            ret = t2h_freebsd_cmsg(&msg, msgp);
        } else {
            ret = 0;
        }
        if (ret == 0) {
            ret = get_errno(safe_sendmsg(fd, &msg, flags));
        }
    } else {
        ret = get_errno(safe_recvmsg(fd, &msg, flags));
        if (!is_error(ret)) {
            len = ret;
            if (msg.msg_control != NULL) {
                ret = h2t_freebsd_cmsg(msgp, &msg);
            } else {
                ret = 0;
            }
            if (!is_error(ret)) {
                msgp->msg_namelen = tswap32(msg.msg_namelen);
                msgp->msg_flags = tswap32(msg.msg_flags);
                if (msg.msg_name != NULL && msg.msg_name != (void *)-1) {
                    ret = host_to_target_sockaddr(tswapal(msgp->msg_name),
                                    msg.msg_name, msg.msg_namelen);
                    if (ret) {
                        goto out;
                    }
                }

                ret = len;
            }
        }
    }

out:
    unlock_iovec(vec, target_vec, count, !send);
out2:
    return ret;
}


static abi_long do_sendrecvmsg(int fd, abi_ulong target_msg,
                               int flags, int send)
{
    abi_long ret;
    struct target_msghdr *msgp;

    if (!lock_user_struct(send ? VERIFY_READ : VERIFY_WRITE,
                          msgp,
                          target_msg,
                          send ? 1 : 0)) {
        return -TARGET_EFAULT;
    }
    ret = do_sendrecvmsg_locked(fd, msgp, flags, send);
    unlock_user_struct(msgp, target_msg, send ? 0 : 1);
    return ret;
}


#endif /* BSD_USER_FREEBSD_OS_SOCKET_H */
