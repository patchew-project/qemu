#include <stdio.h>
#include <stdlib.h>
#include <linux/rtc.h>
#include <fcntl.h>
#include <linux/input.h>
#include <sys/types.h>
#include <unistd.h>
#include <sys/capability.h>

#define ERROR -1

int main()
{

    int fd = open("/dev/rtc", O_RDWR | O_NONBLOCK);

    if (fd == ERROR) {
        perror("open");
        return -1;
    }

    struct rtc_time time = {54, 34, 13, 26, 8, 120, 0, 0, 0};

    if (ioctl(fd, RTC_SET_TIME, &time) == ERROR) {
        perror("ioctl");
        return -1;
    }

    printf("Time set!\n");

    return 0;
}

