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

    int fd = open("/dev/rtc", O_RDWR);

    if (fd == ERROR) {
        perror("open");
        return -1;
    }

    struct rtc_time alarm_time = {13, 35, 12};

    if (ioctl(fd, RTC_ALM_SET, &alarm_time) == ERROR) {
        perror("ioctl");
        return -1;
    }

    printf("Alarm set!\n");

    return 0;
}
