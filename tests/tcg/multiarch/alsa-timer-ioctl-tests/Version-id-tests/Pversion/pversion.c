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

    int version = 0;

    if (ioctl(fd, SNDRV_TIMER_IOCTL_PVERSION, &version) == ERROR) {
        perror("ioctl");
        return -1;
    }

    printf("Timer version: %d\n", version);

    return 0;
}

