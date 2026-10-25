/*
 * A zero-length readv()/writev()/recvmsg() still validates the file
 * descriptor.
 *
 * The kernel checks the file mode before it looks at the vector, so an
 * empty vector on a descriptor opened the wrong way fails with EBADF
 * instead of returning 0.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#define _GNU_SOURCE
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <sys/uio.h>
#include <unistd.h>

int main(void)
{
    int ro = open("/dev/null", O_RDONLY);
    int wo = open("/dev/null", O_WRONLY);
    struct msghdr msg = { 0 };
    int sv[2];

    assert(ro >= 0);
    assert(wo >= 0);

    errno = 0;
    assert(readv(wo, NULL, 0) == -1 && errno == EBADF);
    errno = 0;
    assert(writev(ro, NULL, 0) == -1 && errno == EBADF);

    /* A closed descriptor is rejected all the same. */
    close(wo);
    errno = 0;
    assert(readv(wo, NULL, 0) == -1 && errno == EBADF);

    /* On a usable descriptor an empty vector is a successful no-op. */
    assert(readv(ro, NULL, 0) == 0);

    /* recvmsg() with an empty vector reaches the socket too. */
    assert(socketpair(AF_UNIX, SOCK_DGRAM, 0, sv) == 0);
    close(sv[0]);
    msg.msg_iov = NULL;
    msg.msg_iovlen = 0;
    errno = 0;
    assert(recvmsg(sv[0], &msg, 0) == -1 && errno == EBADF);
    errno = 0;
    assert(recvmsg(sv[1], &msg, MSG_DONTWAIT) == -1 && errno == EAGAIN);

    close(sv[1]);
    close(ro);
    return EXIT_SUCCESS;
}
