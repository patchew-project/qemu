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

    unsigned long interrupt_rate;

    if (ioctl(fd, RTC_IRQP_READ, &interrupt_rate) == ERROR) {
        perror("ioctl");
        return -1;
    }

    printf("Interrupt rate: %d\n", interrupt_rate);

    return 0;
}
