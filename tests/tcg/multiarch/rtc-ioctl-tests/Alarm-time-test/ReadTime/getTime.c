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

    struct rtc_time cur_time;

    if (ioctl(fd, RTC_RD_TIME, &cur_time) < 0) {
        perror("ioctl");
        return -1;
    }

    printf("Second: %d, Minute: %d, Hour: %d, Day: %d, Month: %d, Year: %d\n",
           cur_time.tm_sec, cur_time.tm_min, cur_time.tm_hour,
           cur_time.tm_mday, cur_time.tm_mon, cur_time.tm_year);

    printf("Time set!\n");

    return 0;
}
