#include "qemu/osdep.h"
#include "block/probe.h"
#include "qemu/cutils.h"

static const char *protocol = "host_device";

#ifdef _WIN32
const char *hdev_probe_device(const char *filename, int *score)
{
    assert(score);
    *score = 100;
    if (strstart(filename, "/dev/cdrom", NULL)) {
        return protocol;
    }
    if (is_windows_drive(filename)) {
        return protocol
    }
    *score = 0;
    return protocol;
}
#else
const char *hdev_probe_device(const char *filename, int *score)
{
    struct stat st;
    assert(score);

    /* allow a dedicated CD-ROM driver to match with a higher priority */
    if (strstart(filename, "/dev/cdrom", NULL)) {
        *score = 50;
        return protocol;
    }

    if (stat(filename, &st) >= 0 &&
            (S_ISCHR(st.st_mode) || S_ISBLK(st.st_mode))) {
        *score = 100;
        return protocol;
    }

    *score = 0;
    return protocol;
}
#endif
