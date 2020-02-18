#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <sys/types.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <sound/asound.h>

#define ERROR -1

#define TEST_ALSA_IOCTL(fd, command, argument, supported)     \
    do {                                                      \
        printf("%s:\n", #command);                            \
        if (ioctl(fd, command, argument) == ERROR) {          \
            perror("ioctl");                                  \
            printf("\n");                                     \
            supported = false;                                \
        }                                                     \
    } while (0)

static bool test_pversion(int fd, bool supported)
{
    int version = 0;

    TEST_ALSA_IOCTL(fd, SNDRV_TIMER_IOCTL_PVERSION, &version, supported);
    if (supported) {
        printf("Timer version: %d\n", version);
        printf("\n");
    }
    return supported;
}

static bool test_next_device(int fd, bool supported)
{
    struct snd_timer_id id = {1, 0, 0, 0, 0};

    TEST_ALSA_IOCTL(fd, SNDRV_TIMER_IOCTL_NEXT_DEVICE, &id, supported);
    if (supported) {
        printf("Timer dev_class: %d\n", id.dev_class);
        printf("Timer dev_sclass: %d\n", id.dev_class);
        printf("Timer card: %d\n", id.dev_class);
        printf("Timer device: %d\n", id.dev_class);
        printf("Timer subdevice: %d\n", id.dev_class);
        printf("\n");
    }
    return supported;
}

static bool test_tread(int fd, bool supported)
{
    int tread = 1;

    TEST_ALSA_IOCTL(fd, SNDRV_TIMER_IOCTL_TREAD, &tread, supported);
    if (supported) {
        printf("Enhanced read enabled!\n");
        printf("\n");
    }
    return supported;
}

static bool test_ginfo(int fd, bool supported)
{
    struct snd_timer_id id = {SNDRV_TIMER_CLASS_GLOBAL,
                              SNDRV_TIMER_SCLASS_NONE, -1,
                              SNDRV_TIMER_GLOBAL_SYSTEM, 0};
    struct snd_timer_ginfo ginfo;
    ginfo.tid = id;

    TEST_ALSA_IOCTL(fd, SNDRV_TIMER_IOCTL_GINFO, &ginfo, supported);
    if (supported) {
        printf("Timer flags: %u\n", ginfo.flags);
        printf("Card number: %d\n", ginfo.card);
        printf("Timer identification: %s\n", ginfo.id);
        printf("Timer name: %s\n", ginfo.name);
        printf("Average period resolution: %luns\n", ginfo.resolution);
        printf("Minimal period resolution: %luns\n", ginfo.resolution_min);
        printf("Maximal period resolution: %luns\n", ginfo.resolution_max);
        printf("Active timer clients: %u\n", ginfo.clients);
        printf("\n");
    }
    return supported;
}

static bool test_gparams(int fd, bool supported)
{
    struct snd_timer_id id = {SNDRV_TIMER_CLASS_GLOBAL,
                              SNDRV_TIMER_SCLASS_NONE, -1,
                              SNDRV_TIMER_GLOBAL_SYSTEM, 0};
    struct snd_timer_gparams gparams;
    gparams.tid = id;

    gparams.period_num = 2;
    gparams.period_den = 3;

    TEST_ALSA_IOCTL(fd, SNDRV_TIMER_IOCTL_GPARAMS, &gparams, supported);
    if (supported) {
        printf("Period duration numerator set: %lus\n", gparams.period_num);
        printf("Period duration denominator set: %lus\n", gparams.period_den);
        printf("\n");
    }
    return supported;
}

static bool test_gstatus(int fd, bool supported)
{
    struct snd_timer_id id = {SNDRV_TIMER_CLASS_GLOBAL,
                              SNDRV_TIMER_SCLASS_NONE, -1,
                              SNDRV_TIMER_GLOBAL_SYSTEM, 0};
    struct snd_timer_gstatus gstatus;
    gstatus.tid = id;

    TEST_ALSA_IOCTL(fd, SNDRV_TIMER_IOCTL_GSTATUS, &gstatus, supported);
    if (supported) {
        printf("Current period resolution: %luns\n", gstatus.resolution);
        printf("Period resolution numerator: %lu\n", gstatus.resolution_num);
        printf("Period resolution denominator: %lu\n", gstatus.resolution_den);
        printf("\n");
    }
    return supported;
}

static bool test_select(int fd, bool supported)
{
    struct snd_timer_id id = {SNDRV_TIMER_CLASS_GLOBAL,
                              SNDRV_TIMER_SCLASS_NONE, -1,
                              SNDRV_TIMER_GLOBAL_SYSTEM, 0};
    struct snd_timer_select select;
    select.id = id;

    TEST_ALSA_IOCTL(fd, SNDRV_TIMER_IOCTL_SELECT, &select, supported);
    if (supported) {
        printf("Global timer selected!\n");
        printf("\n");
    }
    return supported;
}

static bool test_info(int fd, bool supported)
{
    struct snd_timer_info info = {0};

    TEST_ALSA_IOCTL(fd, SNDRV_TIMER_IOCTL_INFO, &info, supported);
    if (supported) {
        printf("timer flags: %u\n", info.flags);
        printf("card number: %d\n", info.card);
        printf("timer identificator: %s\n", info.id);
        printf("timer name: %s\n", info.name);
        printf("average period resolution: %luns\n", info.resolution);
        printf("\n");
    }
    return supported;
}

static bool test_params(int fd, bool supported)
{
    struct snd_timer_params params = {0};
    params.ticks = 1;
    params.filter = SNDRV_TIMER_EVENT_TICK;

    TEST_ALSA_IOCTL(fd, SNDRV_TIMER_IOCTL_PARAMS, &params, supported);
    if (supported) {
        printf("Resolution in ticks set: %u\n", params.ticks);
        printf("Event filter set: %d\n", params.filter);
        printf("\n");
    }
    return supported;
}

static bool test_status(int fd, bool supported)
{
    struct snd_timer_status status = {0};

    TEST_ALSA_IOCTL(fd, SNDRV_TIMER_IOCTL_STATUS, &status, supported);
    if (supported) {
        printf("Current period resolution: %dns\n", status.resolution);
        printf("Master tick lost: %d\n", status.lost);
        printf("Read queue overruns: %d\n", status.overrun);
        printf("Queue size: %d\n", status.queue);
        printf("\n");
    }
    return supported;
}

static bool test_start(int fd, bool supported)
{
    TEST_ALSA_IOCTL(fd, SNDRV_TIMER_IOCTL_START, NULL, supported);
    if (supported) {
        printf("Alsa sound timer started!\n");
        printf("\n");
    }
    return supported;
}

static bool test_pause(int fd, bool supported)
{
    TEST_ALSA_IOCTL(fd, SNDRV_TIMER_IOCTL_PAUSE, NULL, supported);
    if (supported) {
        printf("Alsa sound timer paused!\n");
        printf("\n");
    }
    return supported;
}

static bool test_continue(int fd, bool supported)
{
    TEST_ALSA_IOCTL(fd, SNDRV_TIMER_IOCTL_CONTINUE, NULL, supported);
    if (supported) {
        printf("Alsa sound timer continued!\n");
        printf("\n");
    }
    return supported;
}

static bool test_stop(int fd, bool supported)
{
    TEST_ALSA_IOCTL(fd, SNDRV_TIMER_IOCTL_STOP, NULL, supported);
    if (supported) {
        printf("Alsa sound timer stopped!\n");
        printf("\n");
    }
    return supported;
}

int main(int argc, char **argv)
{
    char ioctls[15][35] = {"SNDRV_TIMER_IOCTL_PVERSION",
                           "SNDRV_TIMER_IOCTL_NEXT_DEVICE",
                           "SNDRV_TIMER_IOCTL_TREAD",
                           "SNDRV_TIMER_IOCTL_GINFO",
                           "SNDRV_TIMER_IOCTL_GPARAMS",
                           "SNDRV_TIMER_IOCTL_GSTATUS",
                           "SNDRV_TIMER_IOCTL_SELECT",
                           "SNDRV_TIMER_IOCTL_INFO",
                           "SNDRV_TIMER_IOCTL_PARAMS",
                           "SNDRV_TIMER_IOCTL_STATUS",
                           "SNDRV_TIMER_IOCTL_START",
                           "SNDRV_TIMER_IOCTL_PAUSE",
                           "SNDRV_TIMER_IOCTL_CONTINUE",
                           "SNDRV_TIMER_IOCTL_STOP"};

    bool (*const funcs[]) (int, bool) = {
          test_pversion,
          test_next_device,
          test_tread,
          test_ginfo,
          test_gparams,
          test_gstatus,
          test_select,
          test_info,
          test_params,
          test_status,
          test_start,
          test_pause,
          test_continue,
          test_stop,
          NULL
    };

    int fd = open("/dev/snd/timer", O_RDWR | O_NONBLOCK);

    if (fd == ERROR) {
        perror("open");
        exit(EXIT_FAILURE);
    }

    bool supported = true;

    if (argc > 1) {
        for (int i = 1; i < argc; i++) {
            int j = 0;
            int found = 0;

            for (int j = 0; j < 14; j++) {
                if (!strcmp(argv[i], ioctls[j])) {
                    found = 1;
                    funcs[j](fd, supported);
                }
            }

            if (!found) {
                printf("%s: No such ioctl command!\n", argv[i]);
            }
        }
    } else {
        unsigned int i = 0;

        while (funcs[i++]) {
            funcs[i - 1](fd, supported);
        }
    }

    exit(EXIT_SUCCESS);
}
