/*
 * Linux UFFD-WP support
 *
 * Copyright Virtuozzo GmbH, 2020
 *
 * Authors:
 *  Andrey Gruzdev   <andrey.gruzdev@virtuozzo.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or
 * later.  See the COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"
#include "qemu/bitops.h"
#include "qemu/error-report.h"
#include "qemu/userfaultfd.h"
#include <poll.h>
#include <sys/syscall.h>
#include <sys/ioctl.h>

/**
 * uffd_create_fd: create UFFD file descriptor
 *
 * Returns non-negative file descriptor or negative value in case of an error
 */
int uffd_create_fd(void)
{
    int uffd;
    struct uffdio_api api_struct;
    uint64_t ioctl_mask = BIT(_UFFDIO_REGISTER) | BIT(_UFFDIO_UNREGISTER);

    uffd = syscall(__NR_userfaultfd, O_CLOEXEC | O_NONBLOCK);
    if (uffd < 0) {
        error_report("uffd_create_fd() failed: UFFD not supported");
        return -1;
    }

    api_struct.api = UFFD_API;
    api_struct.features = UFFD_FEATURE_PAGEFAULT_FLAG_WP;
    if (ioctl(uffd, UFFDIO_API, &api_struct)) {
        error_report("uffd_create_fd() failed: "
                     "API version not supported version=%llx errno=%i",
                api_struct.api, errno);
        goto fail;
    }

    if ((api_struct.ioctls & ioctl_mask) != ioctl_mask) {
        error_report("uffd_create_fd() failed: "
                     "PAGEFAULT_FLAG_WP feature missing");
        goto fail;
    }

    return uffd;

fail:
    close(uffd);
    return -1;
}

/**
 * uffd_close_fd: close UFFD file descriptor
 *
 * @uffd: UFFD file descriptor
 */
void uffd_close_fd(int uffd)
{
    assert(uffd >= 0);
    close(uffd);
}

/**
 * uffd_register_memory: register memory range with UFFD
 *
 * Returns 0 in case of success, negative value on error
 *
 * @uffd: UFFD file descriptor
 * @start: starting virtual address of memory range
 * @length: length of memory range
 * @track_missing: generate events on missing-page faults
 * @track_wp: generate events on write-protected-page faults
 */
int uffd_register_memory(int uffd, hwaddr start, hwaddr length,
        bool track_missing, bool track_wp)
{
    struct uffdio_register uffd_register;

    uffd_register.range.start = start;
    uffd_register.range.len = length;
    uffd_register.mode = (track_missing ? UFFDIO_REGISTER_MODE_MISSING : 0) |
                         (track_wp ? UFFDIO_REGISTER_MODE_WP : 0);

    if (ioctl(uffd, UFFDIO_REGISTER, &uffd_register)) {
        error_report("uffd_register_memory() failed: "
                     "start=%0"PRIx64" len=%"PRIu64" mode=%llu errno=%i",
                start, length, uffd_register.mode, errno);
        return -1;
    }

    return 0;
}

/**
 * uffd_unregister_memory: un-register memory range with UFFD
 *
 * Returns 0 in case of success, negative value on error
 *
 * @uffd: UFFD file descriptor
 * @start: starting virtual address of memory range
 * @length: length of memory range
 */
int uffd_unregister_memory(int uffd, hwaddr start, hwaddr length)
{
    struct uffdio_range uffd_range;

    uffd_range.start = start;
    uffd_range.len = length;

    if (ioctl(uffd, UFFDIO_UNREGISTER, &uffd_range)) {
        error_report("uffd_unregister_memory() failed: "
                     "start=%0"PRIx64" len=%"PRIu64" errno=%i",
                start, length, errno);
        return -1;
    }

    return 0;
}

/**
 * uffd_protect_memory: protect/unprotect memory range for writes with UFFD
 *
 * Returns 0 on success or negative value in case of error
 *
 * @uffd: UFFD file descriptor
 * @start: starting virtual address of memory range
 * @length: length of memory range
 * @wp: write-protect/unprotect
 */
int uffd_protect_memory(int uffd, hwaddr start, hwaddr length, bool wp)
{
    struct uffdio_writeprotect uffd_writeprotect;
    int res;

    uffd_writeprotect.range.start = start;
    uffd_writeprotect.range.len = length;
    uffd_writeprotect.mode = (wp ? UFFDIO_WRITEPROTECT_MODE_WP : 0);

    do {
        res = ioctl(uffd, UFFDIO_WRITEPROTECT, &uffd_writeprotect);
    } while (res < 0 && errno == EINTR);
    if (res < 0) {
        error_report("uffd_protect_memory() failed: "
                     "start=%0"PRIx64" len=%"PRIu64" mode=%llu errno=%i",
                start, length, uffd_writeprotect.mode, errno);
        return -1;
    }

    return 0;
}

/**
 * uffd_read_events: read pending UFFD events
 *
 * Returns number of fetched messages, 0 if non is available or
 * negative value in case of an error
 *
 * @uffd: UFFD file descriptor
 * @msgs: pointer to message buffer
 * @count: number of messages that can fit in the buffer
 */
int uffd_read_events(int uffd, struct uffd_msg *msgs, int count)
{
    ssize_t res;
    do {
        res = read(uffd, msgs, count * sizeof(struct uffd_msg));
    } while (res < 0 && errno == EINTR);

    if ((res < 0 && errno == EAGAIN)) {
        return 0;
    }
    if (res < 0) {
        error_report("uffd_read_events() failed: errno=%i", errno);
        return -1;
    }

    return (int) (res / sizeof(struct uffd_msg));
}

/**
 * uffd_poll_events: poll UFFD file descriptor for read
 *
 * Returns true if events are available for read, false otherwise
 *
 * @uffd: UFFD file descriptor
 * @tmo: timeout in milliseconds, 0 for non-blocking operation,
 *       negative value for infinite wait
 */
bool uffd_poll_events(int uffd, int tmo)
{
    int res;
    struct pollfd poll_fd = { .fd = uffd, .events = POLLIN, .revents = 0 };

    do {
        res = poll(&poll_fd, 1, tmo);
    } while (res < 0 && errno == EINTR);

    if (res == 0) {
        return false;
    }
    if (res < 0) {
        error_report("uffd_poll_events() failed: errno=%i", errno);
        return false;
    }

    return (poll_fd.revents & POLLIN) != 0;
}
