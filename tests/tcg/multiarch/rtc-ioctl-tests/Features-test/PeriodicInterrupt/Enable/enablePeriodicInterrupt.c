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

    if (ioctl(fd, RTC_PIE_ON) == ERROR) {
        perror("ioctl");
        return -1;
    }

    printf("Periodic interrupt enabled!\n");

    return 0;
}
