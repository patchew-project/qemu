#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <sound/asound.h>

#define ERROR -1

int main()
{
    int fd = open("/dev/snd/timer", O_RDWR);

    if (fd == ERROR) {
        perror("open");
        return -1;
    }

    struct snd_timer_id id = {SNDRV_TIMER_CLASS_GLOBAL,
                              SNDRV_TIMER_SCLASS_NONE, -1,
                              SNDRV_TIMER_GLOBAL_SYSTEM, 0};

    struct snd_timer_ginfo ginfo;
    ginfo.tid = id;

    if (ioctl(fd, SNDRV_TIMER_IOCTL_GINFO, &ginfo) == ERROR) {
        perror("ioctl");
        return -1;
    }

    printf("flags: %u\n", ginfo.flags);
    printf("card: %d\n", ginfo.card);
    printf("id: %s\n", ginfo.id);
    printf("name: %s\n", ginfo.name);
    printf("reserved0: %lu\n", ginfo.reserved0);
    printf("resolution: %lu\n", ginfo.resolution);
    printf("resolution_min: %lu\n", ginfo.resolution_min);
    printf("reolution_max: %lu\n", ginfo.resolution_max);
    printf("clients: %u\n", ginfo.clients);
    printf("reserved: %s\n", ginfo.reserved);

    return 0;
}
