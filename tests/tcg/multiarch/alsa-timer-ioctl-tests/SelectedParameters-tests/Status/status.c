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

    struct snd_timer_select select;
    select.id = id;


    if (ioctl(fd, SNDRV_TIMER_IOCTL_SELECT, &select) == ERROR) {
        perror("ioctl:");
        return -1;
    }

    struct snd_timer_status status = {0};

    if (ioctl(fd, SNDRV_TIMER_IOCTL_STATUS, &status) == ERROR) {
        perror("ioctl:");
        return -1;
    }

    printf("resolution: %d\n", status.resolution);
    printf("lost: %d\n", status.lost);
    printf("overrun: %d\n", status.overrun);
    printf("queue: %d\n", status.queue);

    return 0;
}
