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

    int fd = open("/dev/rtc", O_RDWR | O_NONBLOCK);

    if (fd == ERROR) {
        perror("open");
        return -1;
    }

    struct rtc_time alarm_time;

    if (ioctl(fd, RTC_ALM_READ, &alarm_time) == ERROR) {
        perror("ioctl");
        return -1;
    }

    printf("Alarm Second: %d, Alarm Minute: %d, Alarm Hour: %d\n",
           alarm_time.tm_sec, alarm_time.tm_min, alarm_time.tm_hour);

    return 0;
}

