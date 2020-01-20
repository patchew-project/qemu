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

    int fd = open("/dev/rtc0", O_RDONLY);

    if (fd == ERROR) {
        perror("open");
        return -1;
    }

    struct rtc_pll_info info = {1, 1, 1, 1, 1, 1, 1};

    if (ioctl(fd, RTC_PLL_SET, &info) == ERROR) {
        perror("ioctl");
        return -1;
    }

    printf("Pll correction set!\n");

    return 0;
}
