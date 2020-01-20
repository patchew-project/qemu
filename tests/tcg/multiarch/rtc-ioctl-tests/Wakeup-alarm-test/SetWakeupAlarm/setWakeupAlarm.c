#include <stdio.h>
#include <stdlib.h>
#include <linux/rtc.h>
#include <fcntl.h>
#include <linux/input.h>
#include <sys/types.h>
#include <unistd.h>

#define ERROR -1

int main()
{

    int fd = open("/dev/rtc", O_RDONLY);

    if (fd == ERROR) {
        perror("open");
        return -1;
    }

    struct rtc_time time = {25, 30, 10, 27, 8, 12, 0, 0, 0};

    struct rtc_wkalrm alarm = {0, 0, time};

    if (ioctl(fd, RTC_WKALM_SET, &alarm) == ERROR) {
        perror("ioctl");
        return -1;
    }

    printf("Wakeup alarm set!\n");

    return 0;
}

