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

    struct rtc_wkalrm alarm;

    if (ioctl(fd, RTC_WKALM_RD, &alarm) == ERROR) {
        perror("ioctl");
        return -1;
    }

    printf("Alarm enabled: %d, Alarm pending: %d, Alarm second %d,
            Alarm minute: %d, Alarm hour: %d, Alarm day: %d,
            Alarm month: %d\n",
            alarm.enabled, alarm.pending, alarm.time.tm_sec, alarm.time.tm_min,
            alarm.time.tm_hour, alarm.time.tm_mday, alarm.time.tm_mon,
            alarm.time.tm_year);

    return 0;
}
