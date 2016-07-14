#include "qemu/osdep.h"
#include "block/probe.h"

static const char *protocol = "host_cdrom";

#if defined(__FreeBSD__) || defined(__FreeBSD_kernel__)
const char *cdrom_probe_device(const char *filename, int *score)
{
    assert(score);
    if (strstart(filename, "/dev/cd", NULL) ||
        strstart(filename, "/dev/acd", NULL)) {
        *score = 100;
        return protocol;
    }
    return 0;
}
#elif defined(__linux__)
#include <sys/ioctl.h>
#include <linux/cdrom.h>
const char *cdrom_probe_device(const char *filename, int *score)
{
    int fd, ret;
    struct stat st;
    assert(score);
    *score = 0;

    fd = qemu_open(filename, O_RDONLY | O_NONBLOCK);
    if (fd < 0) {
        goto out;
    }
    ret = fstat(fd, &st);
    if (ret == -1 || !S_ISBLK(st.st_mode)) {
        goto outc;
    }

    /* Attempt to detect via a CDROM specific ioctl */
    ret = ioctl(fd, CDROM_DRIVE_STATUS, CDSL_CURRENT);
    if (ret >= 0) {
        *score = 100;
    }

outc:
    qemu_close(fd);
out:
    return protocol;
}
#endif
