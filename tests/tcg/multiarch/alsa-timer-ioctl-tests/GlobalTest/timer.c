#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

#include <sys/ioctl.h>

#include <errno.h>
#include <string.h>

#include <sound/asound.h>

static void build_system_timer_id(struct snd_timer_id *tid)
{
    tid->dev_class = SNDRV_TIMER_CLASS_GLOBAL;
    tid->dev_sclass = SNDRV_TIMER_SCLASS_NONE;
    tid->card = -1;
    tid->device = SNDRV_TIMER_GLOBAL_SYSTEM;
    tid->subdevice = 0;
}

#define TEST_IOCTL(fd, command, argument, expected)                    \
    do {                                                               \
        if (ioctl(fd, command, argument) < 0 && errno != expected) {   \
            printf("%s: %s\n", #command, strerror(errno));             \
            return false;                                              \
        }                                                              \
        printf("%s: Test passed!\n", #command);                        \
        return true;                                                   \
    } while (0)                                                         \

static bool check_pversion(int fd)
{
    int version = 0;
    TEST_IOCTL(fd, SNDRV_TIMER_IOCTL_PVERSION, &version, 0);
}

static bool check_next_device(int fd)
{
    struct snd_timer_id id = {0};
    TEST_IOCTL(fd, SNDRV_TIMER_IOCTL_NEXT_DEVICE, &id, 0);
}

static bool check_tread(int fd)
{
    int tread = 1;
    TEST_IOCTL(fd, SNDRV_TIMER_IOCTL_TREAD, &tread, 0);
}

static bool check_ginfo(int fd)
{
    struct snd_timer_ginfo ginfo = {0};
    build_system_timer_id(&ginfo.tid);
    TEST_IOCTL(fd, SNDRV_TIMER_IOCTL_GINFO, &ginfo, 0);
}

static bool check_gparams(int fd)
{
    struct snd_timer_gparams gparams = {0};
    build_system_timer_id(&gparams.tid);
    TEST_IOCTL(fd, SNDRV_TIMER_IOCTL_GPARAMS, &gparams, ENOSYS);
}

static bool check_gstatus(int fd)
{
    struct snd_timer_gstatus gstatus = {0};
    build_system_timer_id(&gstatus.tid);
    TEST_IOCTL(fd, SNDRV_TIMER_IOCTL_GSTATUS, &gstatus, 0);
}

static bool check_select(int fd)
{
    struct snd_timer_select select = {0};
    build_system_timer_id(&select.id);
    TEST_IOCTL(fd, SNDRV_TIMER_IOCTL_SELECT, &select, 0);
}

static bool check_info(int fd)
{
    struct snd_timer_info info = {0};
    TEST_IOCTL(fd, SNDRV_TIMER_IOCTL_INFO, &info, 0);
}

static bool check_params(int fd)
{
    struct snd_timer_params params = {0};
    params.ticks = 1;
    params.filter = SNDRV_TIMER_EVENT_TICK;
    TEST_IOCTL(fd, SNDRV_TIMER_IOCTL_PARAMS, &params, 0);
}

static bool check_status(int fd)
{
    struct snd_timer_status status = {0};
    TEST_IOCTL(fd, SNDRV_TIMER_IOCTL_STATUS, &status, 0);
}

static bool check_start(int fd)
{
    TEST_IOCTL(fd, SNDRV_TIMER_IOCTL_START, NULL, 0);
}

static bool check_stop(int fd)
{
    TEST_IOCTL(fd, SNDRV_TIMER_IOCTL_STOP, NULL, 0);
}

static bool check_continue(int fd)
{
    TEST_IOCTL(fd, SNDRV_TIMER_IOCTL_CONTINUE, NULL, 0);
}

static bool check_pause(int fd)
{
    TEST_IOCTL(fd, SNDRV_TIMER_IOCTL_PAUSE, NULL, 0);
}

int main(void)
{
    bool (*const funcs[])(int) = {
        check_pversion,
        check_next_device,
        check_tread,
        check_ginfo,
        check_gparams,
        check_gstatus,
        check_select,
        check_info,
        check_params,
        check_status,
        check_start,
        check_pause,
        check_continue,
        check_stop,
        NULL,
    };
    unsigned int i;
    int fd;

    fd = open("/dev/snd/timer", O_RDONLY);
    if (fd < 0) {
        printf("%s\n", strerror(errno));
        return EXIT_FAILURE;
    }

    i = 0;
    while (funcs[i]) {
        if (!funcs[i++](fd)) {
            printf("Timer test aborts.\n");
            return EXIT_FAILURE;
        }
    }

    return EXIT_SUCCESS;
}
