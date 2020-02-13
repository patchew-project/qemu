#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <linux/rtc.h>
#include <fcntl.h>
#include <linux/input.h>
#include <sys/types.h>
#include <unistd.h>
#include <string.h>

#define ERROR -1

#define TEST_RTC_IOCTL(fd, command, argument, supported)      \
    do {                                                      \
        printf("%s:\n", #command);                            \
        if (ioctl(fd, command, argument) == ERROR) {          \
            perror("ioctl");                                  \
            printf("\n");                                     \
            supported = false;                                \
        }                                                     \
    } while (0)

static bool test_aie_on(int fd, bool supported)
{
    TEST_RTC_IOCTL(fd, RTC_AIE_ON, NULL, supported);
    if (supported) {
        printf("Alarm interrupt enabled!\n\n");
    }
    return supported;
}

static bool test_aie_off(int fd, bool supported)
{
    TEST_RTC_IOCTL(fd, RTC_AIE_OFF, NULL, supported);
    if (supported) {
        printf("Alarm interrupt disabled!\n\n");
    }
    return supported;
}

static bool test_pie_on(int fd, bool supported)
{
    TEST_RTC_IOCTL(fd, RTC_PIE_ON, NULL, supported);
    if (supported) {
        printf("Periodic interrupt enabled!\n\n");
    }
    return supported;
}

static bool test_pie_off(int fd, bool supported)
{
    TEST_RTC_IOCTL(fd, RTC_PIE_OFF, NULL, supported);
    if (supported) {
        printf("Periodic interrupt disabled!\n\n");
    }
    return supported;
}

static bool test_uie_on(int fd, bool supported)
{
    TEST_RTC_IOCTL(fd, RTC_UIE_ON, NULL, supported);
    if (supported) {
        printf("Update interrupt enabled!\n\n");
    }
    return supported;
}

static bool test_uie_off(int fd, bool supported)
{
    TEST_RTC_IOCTL(fd, RTC_UIE_OFF, NULL, supported);
    if (supported) {
        printf("Update interrupt disabled!\n\n");
    }
    return supported;
}

static bool test_wie_on(int fd, bool supported)
{
    TEST_RTC_IOCTL(fd, RTC_WIE_ON, NULL, supported);
    if (supported) {
        printf("Watchdog interrupt enabled!\n\n");
    }
    return supported;
}

static bool test_wie_off(int fd, bool supported)
{
    TEST_RTC_IOCTL(fd, RTC_WIE_OFF, NULL, supported);
    if (supported) {
        printf("Watchdog interrupt disabled!\n\n");
    }
    return supported;
}

static bool test_set_time(int fd, bool supported)
{
    struct rtc_time alarm_time = {54, 34, 13, 26, 8, 120, 0, 0, 0};

    TEST_RTC_IOCTL(fd, RTC_SET_TIME, &alarm_time, supported);
    if (supported) {
        printf("Time set:\n");
        printf("Second: %d, Minute: %d, Hour: %d, "
               "Day: %d, Month: %d, Year: %d\n\n",
               alarm_time.tm_sec, alarm_time.tm_min, alarm_time.tm_hour,
               alarm_time.tm_mday, alarm_time.tm_mon, alarm_time.tm_year);
    }
    return supported;
}

static bool test_rd_time(int fd, bool supported)
{
    struct rtc_time alarm_time;

    TEST_RTC_IOCTL(fd, RTC_RD_TIME, &alarm_time, supported);
    if (supported) {
        printf("Time read:\n");
        printf("Second: %d, Minute: %d, Hour: %d, "
               "Day: %d, Month: %d, Year: %d\n\n",
               alarm_time.tm_sec, alarm_time.tm_min, alarm_time.tm_hour,
               alarm_time.tm_mday, alarm_time.tm_mon, alarm_time.tm_year);
    }
    return supported;
}

static bool test_alm_set(int fd, bool supported)
{
    struct rtc_time alarm_time = {13, 35, 12};

    TEST_RTC_IOCTL(fd, RTC_ALM_SET, &alarm_time, supported);
    if (supported) {
        printf("Alarm time set:\n");
        printf("Second: %d, Minute: %d, Hour: %d\n\n",
               alarm_time.tm_sec, alarm_time.tm_min, alarm_time.tm_hour);
    }
    return supported;
}

static bool test_alm_read(int fd, bool supported)
{
    struct rtc_time alarm_time;

    TEST_RTC_IOCTL(fd, RTC_ALM_READ, &alarm_time, supported);
    if (supported) {
        printf("Alarm time read:\n");
        printf("Second: %d, Minute: %d, Hour: %d\n\n",
               alarm_time.tm_sec, alarm_time.tm_min, alarm_time.tm_hour);
    }
    return supported;
}

static bool test_irqp_set(int fd, bool supported)
{
    unsigned long interrupt_rate = 32;

    TEST_RTC_IOCTL(fd, RTC_IRQP_SET, interrupt_rate, supported);
    if (supported) {
        printf("Periodic interrupt set: %lu\n\n", interrupt_rate);
    }
    return supported;
}

static bool test_irqp_read(int fd, bool supported)
{
    unsigned long interrupt_rate;

    TEST_RTC_IOCTL(fd, RTC_IRQP_READ, &interrupt_rate, supported);
    if (supported) {
        printf("Periodic interrupt read: %lu\n\n", interrupt_rate);
    }
    return supported;
}

static bool test_epoch_set(int fd, bool supported)
{
    unsigned long epoch = 5;

    TEST_RTC_IOCTL(fd, RTC_EPOCH_SET, epoch, supported);
    if (supported) {
        printf("Epoch set: %lu\n\n", epoch);
    }
    return supported;
}

static bool test_epoch_read(int fd, bool supported)
{
    unsigned long epoch;

    TEST_RTC_IOCTL(fd, RTC_EPOCH_READ, epoch, supported);
    if (supported) {
        printf("Epoch read: %lu\n\n", epoch);
    }
    return supported;
}

static bool test_wkalm_set(int fd, bool supported)
{
    struct rtc_time time = {25, 30, 10, 27, 8, 12, 0, 0, 0};
    struct rtc_wkalrm alarm = {0, 0, time};

    TEST_RTC_IOCTL(fd, RTC_WKALM_SET, &alarm, supported);
    if (supported) {
        printf("Wakeup alarm set:\n");
        printf("Enabled: %d, Pending: %d\n", alarm.enabled, alarm.pending);
        printf("Second: %d, Minute: %d, Hour: %d\n\n",
               alarm.time.tm_sec, alarm.time.tm_min, alarm.time.tm_hour);
    }
    return supported;
}

static bool test_wkalm_rd(int fd, bool supported)
{
    struct rtc_wkalrm alarm;

    TEST_RTC_IOCTL(fd, RTC_WKALM_RD, &alarm, supported);
    if (supported) {
        printf("Wakeup alarm read:\n");
        printf("Enabled: %d, Pending: %d\n", alarm.enabled, alarm.pending);
        printf("Second: %d, Minute: %d, Hour: %d\n\n",
               alarm.time.tm_sec, alarm.time.tm_min, alarm.time.tm_hour);
    }
    return supported;
}

static bool test_pll_set(int fd, bool supported)
{
    struct rtc_pll_info info = {1, 5, 50, 10, 20, 10, 15};

    TEST_RTC_IOCTL(fd, RTC_PLL_SET, &info, supported);
    if (supported) {
        printf("Pll correction set:\n");
        printf("Pll ctrl: %d, Pll value: %d, Pll max %d, "
               "Pll min: %d, Pll posmult: %d, Pll negmult: %d, "
               "Pll clock: %lu\n\n",
               info.pll_ctrl, info.pll_value, info.pll_max,
               info.pll_min, info.pll_posmult, info.pll_negmult,
               info.pll_clock);
    }
    return supported;
}

static bool test_pll_get(int fd, bool supported)
{
    struct rtc_pll_info info;

    TEST_RTC_IOCTL(fd, RTC_PLL_GET, &info, supported);
    if (supported) {
        printf("Pll correction read:\n");
        printf("Pll ctrl: %d, Pll value: %d, Pll max %d, "
               "Pll min: %d, Pll posmult: %d, Pll negmult: %d, "
               "Pll clock: %lu\n\n",
               info.pll_ctrl, info.pll_value, info.pll_max,
               info.pll_min, info.pll_posmult, info.pll_negmult,
               info.pll_clock);
    }
    return supported;
}

static bool test_vl_read(int fd, bool supported)
{
    int voltage_low;

    TEST_RTC_IOCTL(fd, RTC_VL_READ, &voltage_low, supported);
    if (supported) {
        printf("Voltage low: %d\n\n", voltage_low);
    }
    return supported;
}

static bool test_vl_clear(int fd, bool supported)
{
    TEST_RTC_IOCTL(fd, RTC_VL_CLR, NULL, supported);
    if (supported) {
        printf("Voltage low cleared!\n");
    }
    return supported;
}

int main(int argc, char **argv)
{
    char ioctls[23][15] = {"RTC_AIE_ON", "RTC_AIE_OFF",
                           "RTC_UIE_ON", "RTC_UIE_OFF",
                           "RTC_PIE_ON", "RTC_PIE_OFF",
                           "RTC_WIE_ON", "RTC_WIE_OFF",
                           "RTC_ALM_SET", "RTC_ALM_READ",
                           "RTC_RD_TIME", "RTC_SET_TIME",
                           "RTC_IRQP_READ", "RTC_IRQP_SET",
                           "RTC_EPOCH_READ", "RTC_EPOCH_SET",
                           "RTC_WKALM_SET", "RTC_WKALM_RD",
                           "RTC_PLL_GET", "RTC_PLL_SET",
                           "RTC_VL_READ", "RTC_VL_CLR"};

    bool (*const funcs[]) (int, bool) = {
          test_aie_on,
          test_aie_off,
          test_uie_on,
          test_uie_off,
          test_pie_on,
          test_pie_off,
          test_wie_on,
          test_wie_off,
          test_alm_set,
          test_alm_read,
          test_rd_time,
          test_set_time,
          test_irqp_read,
          test_irqp_set,
          test_epoch_read,
          test_epoch_set,
          test_wkalm_set,
          test_wkalm_rd,
          test_pll_get,
          test_pll_set,
          test_vl_read,
          test_vl_clear,
          NULL
    };

    int fd = open("/dev/rtc", O_RDWR | O_NONBLOCK);

    if (fd == ERROR) {
        perror("open");
        exit(EXIT_FAILURE);
    }

    bool supported = true;

    if (argc > 1) {
        for (int i = 1; i < argc; i++) {
            int j = 0;
            int found = 0;

            for (int j = 0; j < 22; j++) {
                if (!strcmp(argv[i], ioctls[j])) {
                    found = 1;
                    funcs[j](fd, supported);
                }
            }

            if (!found) {
                printf("%s: No such ioctl command!\n", argv[i]);
            }
        }
    } else {
        unsigned int i = 0;

        while (funcs[i++]) {
            funcs[i - 1](fd, supported);
        }
    }

    exit(EXIT_SUCCESS);
}
