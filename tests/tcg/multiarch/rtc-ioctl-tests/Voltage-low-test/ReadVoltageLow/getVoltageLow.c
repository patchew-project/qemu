#include <stdio.h>
#include <stdlib.h>
#include <linux/rtc.h>
#include <fcntl.h>
#include <linux/input.h>
#include <sys/types.h>
#include <unistd.h>
#include <linux/ioctl.h>

#define ERROR -1

int main()
{

    int fd = open("/proc/driver/rtc", O_RDONLY);

    if (fd == ERROR) {
        perror("open");
        return -1;
    }

    int voltage;

    if (ioctl(fd, RTC_VL_READ, &voltage) == ERROR) {
        perror("ioctl");
        return -1;
    }

    printf("Voltage low: %d\n", voltage);

    return 0;
}
