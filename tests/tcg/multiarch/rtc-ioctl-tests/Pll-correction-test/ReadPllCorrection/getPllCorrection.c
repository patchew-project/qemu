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

    int fd = open("/dev/rtc", O_RDONLY);

    if (fd == ERROR) {
        perror("open");
        return -1;
    }

    struct rtc_pll_info info;

    if (ioctl(fd, RTC_PLL_GET, &info) == ERROR) {
        perror("ioctl");
        return -1;
    }

    printf("Pll control: %d, Pll value: %d, Pll max: %d,
            Pll min: %d, Pll posmult: %d, Pll negmult: %d,  Pll clock: %ld\n",
            info.pll_ctrl, info.pll_value, info.pll_max, info.pll_min,
            info.pll_posmult, info.pll_negmult, info.pll_clock);

    return 0;
}
