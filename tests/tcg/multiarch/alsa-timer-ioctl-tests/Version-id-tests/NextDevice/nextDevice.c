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

    struct snd_timer_id id = {1, 0, 0, 0, 0};

    if (ioctl(fd, SNDRV_TIMER_IOCTL_NEXT_DEVICE, &id) == ERROR) {
        perror("ioctl");
        return -1;
    }

    printf("Timer dev_class: %d\n", id.dev_class);
    printf("Timer dev_sclass: %d\n", id.dev_class);
    printf("Timer card: %d\n", id.dev_class);
    printf("Timer device: %d\n", id.dev_class);
    printf("Timer subdevice: %d\n", id.dev_class);

    return 0;
}

