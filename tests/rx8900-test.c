/*
 * QTest testcase for the Epson RX8900SA/CE RTC
 *
 * Copyright (c) 2016 IBM Corporation
 * Authors:
 *  Alastair D'Silva <alastair@d-silva.org>
 *
 * This code is licensed under the GPL version 2 or later.  See
 * the COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"
#include "hw/timer/rx8900_regs.h"
#include "libqtest.h"
#include "libqos/i2c.h"
#include "qemu/timer.h"

#define IMX25_I2C_0_BASE 0x43F80000
#define RX8900_TEST_ID "rx8900-test"
#define RX8900_ADDR 0x32
#define RX8900_INTERRUPT_OUT "rx8900-interrupt-out"
#define RX8900_FOUT_ENABLE "rx8900-fout-enable"
#define RX8900_FOUT "rx8900-fout"

static I2CAdapter *i2c;
static uint8_t addr;

static inline uint8_t bcd2bin(uint8_t x)
{
    return (x & 0x0f) + (x >> 4) * 10;
}

static inline uint8_t bin2bcd(uint8_t x)
{
    return (x / 10 << 4) | (x % 10);
}

static void qmp_rx8900_set_temperature(const char *id, double value)
{
    QDict *response;

    response = qmp("{ 'execute': 'qom-set', 'arguments': { 'path': %s, "
                   "'property': 'temperature', 'value': %f } }", id, value);
    g_assert(qdict_haskey(response, "return"));
    QDECREF(response);
}

static void qmp_rx8900_set_voltage(const char *id, double value)
{
    QDict *response;

    response = qmp("{ 'execute': 'qom-set', 'arguments': { 'path': %s, "
                   "'property': 'voltage', 'value': %f } }", id, value);
    g_assert(qdict_haskey(response, "return"));
    QDECREF(response);
}

/**
 * Read an RX8900 register
 * @param reg the address of the register
 * @return the value of the register
 */
static uint8_t read_register(RX8900Addresses reg)
{
    uint8_t val;
    uint8_t reg_address = (uint8_t)reg;

    i2c_send(i2c, addr, &reg_address, 1);
    i2c_recv(i2c, addr, &val, 1);

    return val;
}

/**
 * Write to an RX8900 register
 * @param reg the address of the register
 * @param val the value to write
 */
static uint8_t write_register(RX8900Addresses reg, uint8_t val)
{
    uint8_t buf[2];

    buf[0] = reg;
    buf[1] = val;

    i2c_send(i2c, addr, buf, 2);

    return val;
}

/**
 * Set bits in a register
 * @param reg the address of the register
 * @param mask a mask of the bits to set
 */
static void set_bits_in_register(RX8900Addresses reg, uint8_t mask)
{
    uint8_t value = read_register(reg);
    value |= mask;
    write_register(reg, value);
}

/**
 * Clear bits in a register
 * @param reg the address of the register
 * @param mask a mask of the bits to set
 */
static void clear_bits_in_register(RX8900Addresses reg, uint8_t mask)
{
    uint8_t value = read_register(reg);
    value &= ~mask;
    write_register(reg, value);
}

/**
 * Read a number of sequential RX8900 registers
 * @param reg the address of the first register
 * @param buf (out) an output buffer to stash the register values
 * @param count the number of registers to read
 */
static void read_registers(RX8900Addresses reg, uint8_t *buf, uint8_t count)
{
    uint8_t reg_address = (uint8_t)reg;

    i2c_send(i2c, addr, &reg_address, 1);
    i2c_recv(i2c, addr, buf, count);
}

/**
 * Write to a sequential number of RX8900 registers
 * @param reg the address of the first register
 * @param buffer a buffer of values to write
 * @param count the sumber of registers to write
 */
static void write_registers(RX8900Addresses reg, uint8_t *buffer, uint8_t count)
{
    uint8_t buf[RX8900_NVRAM_SIZE + 1];

    buf[0] = (uint8_t)reg;
    memcpy(buf + 1, buffer, count);

    i2c_send(i2c, addr, buf, count + 1);
}

/**
 * Set the time on the RX8900
 * @param secs the seconds to set
 * @param mins the minutes to set
 * @param hours the hours to set
 * @param weekday the day of the week to set (0 = Sunday)
 * @param day the day of the month to set
 * @param month the month to set
 * @param year the year to set
 */
static void set_time(uint8_t secs, uint8_t mins, uint8_t hours,
        uint8_t weekday, uint8_t day, uint8_t month, uint8_t year)
{
    uint8_t buf[7];

    buf[0] = bin2bcd(secs);
    buf[1] = bin2bcd(mins);
    buf[2] = bin2bcd(hours);
    buf[3] = BIT(weekday);
    buf[4] = bin2bcd(day);
    buf[5] = bin2bcd(month);
    buf[6] = bin2bcd(year);

    write_registers(SECONDS, buf, 7);
}


/**
 * Check basic communication
 */
static void send_and_receive(void)
{
    uint8_t buf[7];
    time_t now = time(NULL);
    struct tm *tm_ptr;

    /* retrieve the date */
    read_registers(SECONDS, buf, 7);

    tm_ptr = gmtime(&now);

    /* check retrieved time against local time */
    g_assert_cmpuint(bcd2bin(buf[0]), == , tm_ptr->tm_sec);
    g_assert_cmpuint(bcd2bin(buf[1]), == , tm_ptr->tm_min);
    g_assert_cmpuint(bcd2bin(buf[2]), == , tm_ptr->tm_hour);
    g_assert_cmpuint(bcd2bin(buf[4]), == , tm_ptr->tm_mday);
    g_assert_cmpuint(bcd2bin(buf[5]), == , 1 + tm_ptr->tm_mon);
    g_assert_cmpuint(2000 + bcd2bin(buf[6]), == , 1900 + tm_ptr->tm_year);
}

/**
 * Check that the temperature can be altered via properties
 */
static void check_temperature(void)
{
   /* Check the initial temperature is 25C */
    uint8_t temperature;

    temperature = read_register(TEMPERATURE);
    g_assert_cmpuint(temperature, == , 133);

    /* Set the temperature to 40C and check the temperature again */
    qmp_rx8900_set_temperature(RX8900_TEST_ID, 40.0f);
    temperature = read_register(TEMPERATURE);
    g_assert_cmpuint(temperature, == , 157);
}

/**
 * Check that the time rolls over correctly
 */
static void check_rollover(void)
{
    uint8_t buf[7];


    set_time(59, 59, 23, 1, 29, 2, 16);

    /* Wait for the clock to rollover */
    sleep(2);

    memset(buf, 0, sizeof(buf));

    /* Check that the clock rolled over */

    read_registers(SECONDS, buf, 7);

    /* Ignore seconds as there may be some noise,
     * we expect 00:00:xx Tuesday 1/3/2016
     */
    g_assert_cmpuint(bcd2bin(buf[1]), == , 0); /* minutes */
    g_assert_cmpuint(bcd2bin(buf[2]), == , 0); /* hours */
    g_assert_cmpuint(bcd2bin(buf[3]), == , 0x04); /* weekday */
    g_assert_cmpuint(bcd2bin(buf[4]), == , 1); /* day */
    g_assert_cmpuint(bcd2bin(buf[5]), == , 3); /* month */
    g_assert_cmpuint(bcd2bin(buf[6]), == , 16); /* year */
}

uint32_t interrupt_counts[RX8900_INTERRUPT_SOURCES];

/**
 * Reset the interrupt counts
 */
static void count_reset(void)
{
    for (int source = 0; source < RX8900_INTERRUPT_SOURCES; source++) {
        interrupt_counts[source] = 0;
    }
}

/**
 * Handle an RX8900 interrupt (update the counts for that interrupt type)
 */
static void handle_interrupt(void *opaque, const char *name, int irq,
        bool level)
{
    if (!level) {
        return;
    }

    uint8_t flags = read_register(FLAG_REGISTER);

    for (int flag = 0; flag < 8; flag++) {
        if (flags & BIT(flag)) {
            interrupt_counts[flag]++;
        }
    }

    write_register(FLAG_REGISTER, 0x00);
}

uint32_t fout_counts;

/**
 * Handle an Fout state change
 */
static void handle_fout(void *opaque, const char *name, int irq, bool level)
{
    if (!level) {
        return;
    }

    fout_counts++;
}

/**
 * Reset the fout count
 */
static void fout_count_reset(void)
{
    fout_counts = 0;
}


/**
 * Sleep for some real time while counting interrupts
 * @param delay the delay in microseconds
 * @param loop the loop time in microseconds
 */
static void wait_for(uint64_t delay, uint64_t loop)
{
    struct timeval end, now, increment;

    increment.tv_sec = delay / 1000000;
    increment.tv_usec = delay % 1000000;

    gettimeofday(&now, NULL);
    timeradd(&now, &increment, &end);

    while (gettimeofday(&now, NULL), timercmp(&now, &end, <)) {
        clock_step(loop * 1000);
        usleep(loop);
    }
}

/**
 * Sleep for some emulated time while counting interrupts
 * @param delay the delay in nanoseconds
 * @param loop the loop time in nanoseconds
 */
static void wait_cycles(uint64_t delay, uint64_t loop)
{
    uint64_t counter;

    for (counter = 0; counter < delay; counter += loop) {
        clock_step(loop);
    }
}


/**
 * Check that when the update timer interrupt is disabled, that no interrupts
 * occur
 */
static void check_update_interrupt_disabled(void)
{
    /* Disable the update interrupt */
    clear_bits_in_register(CONTROL_REGISTER, CTRL_MASK_UIE);

    /* Wait for the clock to rollover, this will cover both seconds & minutes
     */
    set_time(59, 59, 23, 1, 29, 2, 16);

    count_reset();
    wait_for(2 * 1000000, 1000);

    g_assert_cmpuint(interrupt_counts[FLAG_REG_UF], ==, 0);
    g_assert_cmpuint(interrupt_counts[FLAG_REG_AF], ==, 0);
}


/**
 * Check that when the update timer interrupt is enabled and configured for
 * per second updates, that we get the appropriate number of interrupts
 */
static void check_update_interrupt_seconds(void)
{
    set_time(59, 59, 23, 1, 29, 2, 16);

    /* Enable the update interrupt for per second updates */
    clear_bits_in_register(EXTENSION_REGISTER, EXT_MASK_USEL);
    set_bits_in_register(CONTROL_REGISTER, CTRL_MASK_UIE);

    count_reset();
    wait_for(5.1f * 1000000ULL, 1000);

    /* Disable the update interrupt */
    clear_bits_in_register(CONTROL_REGISTER, CTRL_MASK_UIE);

    g_assert_cmpuint(interrupt_counts[FLAG_REG_UF], >=, 5);
    g_assert_cmpuint(interrupt_counts[FLAG_REG_UF], <=, 6);
    g_assert_cmpuint(interrupt_counts[FLAG_REG_AF], ==, 0);
}

/**
 * Check that when the update timer interrupt is enabled and configured for
 * per minute updates, that we get the appropriate number of interrupts
 */
static void check_update_interrupt_minutes(void)
{
    set_time(59, 59, 23, 1, 29, 2, 16);

    /* Enable the update interrupt for per minute updates */
    set_bits_in_register(EXTENSION_REGISTER, EXT_MASK_USEL);
    set_bits_in_register(CONTROL_REGISTER, CTRL_MASK_UIE);

    count_reset();
    wait_for(5 * 1000000ULL, 1000);

    /* Disable the update interrupt */
    clear_bits_in_register(CONTROL_REGISTER, CTRL_MASK_UIE);

    g_assert_cmpuint(interrupt_counts[FLAG_REG_UF], ==, 1);
    g_assert_cmpuint(interrupt_counts[FLAG_REG_AF], ==, 0);
}


/**
 * Check that when the alarm timer interrupt is disabled, that no interrupts
 * occur
 */
static void check_alarm_interrupt_disabled(void)
{
    /* Disable the alarm interrupt */
    clear_bits_in_register(CONTROL_REGISTER, CTRL_MASK_AIE);

    /* Set an alarm for midnight */
    uint8_t buf[3];

    buf[0] = bin2bcd(0); /* minutes */
    buf[1] = bin2bcd(0); /* hours */
    buf[2] = bin2bcd(1); /* day */

    write_registers(ALARM_MINUTE, buf, 3);

    /* Wait for the clock to rollover */
    set_time(59, 59, 23, 1, 29, 2, 16);

    count_reset();
    wait_for(2 * 1000000, 1000);

    g_assert_cmpuint(interrupt_counts[FLAG_REG_UF], ==, 0);
    g_assert_cmpuint(interrupt_counts[FLAG_REG_AF], ==, 0);
}

/**
 * Check that when the alarm timer interrupt is enabled, that an interrupt
 * occurs
 */
static void check_alarm_interrupt_day_of_month(void)
{

    /* Set an alarm for midnight */
    uint8_t buf[3];

    buf[0] = bin2bcd(0); /* minutes */
    buf[1] = bin2bcd(0); /* hours */
    buf[2] = bin2bcd(1); /* day */

    write_registers(ALARM_MINUTE, buf, 3);

    /* Set alarm to day of month mode */
    set_bits_in_register(EXTENSION_REGISTER, EXT_MASK_WADA);

    /* Enable the alarm interrupt */
    set_bits_in_register(CONTROL_REGISTER, CTRL_MASK_AIE);

    /* Wait for the clock to rollover */
    set_time(59, 59, 23, 1, 29, 2, 16);

    count_reset();
    wait_for(2 * 1000000, 1000);

    /* Disable the alarm interrupt */
    clear_bits_in_register(CONTROL_REGISTER, CTRL_MASK_AIE);

    g_assert_cmpuint(interrupt_counts[FLAG_REG_UF], ==, 0);
    g_assert_cmpuint(interrupt_counts[FLAG_REG_AF], ==, 1);
}

/**
 * Check that when the alarm timer interrupt is enabled, that an interrupt
 * does not occur
 */
static void check_alarm_interrupt_day_of_month_negative(void)
{

    /* Set an alarm for midnight */
    uint8_t buf[3];

    buf[0] = bin2bcd(0); /* minutes */
    buf[1] = bin2bcd(0); /* hours */
    buf[2] = bin2bcd(2); /* day */

    write_registers(ALARM_MINUTE, buf, 3);

    /* Set alarm to day of month mode */
    set_bits_in_register(EXTENSION_REGISTER, EXT_MASK_WADA);

    /* Enable the alarm interrupt */
    set_bits_in_register(CONTROL_REGISTER, CTRL_MASK_AIE);

    /* Wait for the clock to rollover */
    set_time(59, 59, 23, 1, 29, 2, 16);

    count_reset();
    wait_for(2 * 1000000, 1000);

    /* Disable the alarm interrupt */
    clear_bits_in_register(CONTROL_REGISTER, CTRL_MASK_AIE);

    g_assert_cmpuint(interrupt_counts[FLAG_REG_UF], ==, 0);
    g_assert_cmpuint(interrupt_counts[FLAG_REG_AF], ==, 0);
}

/**
 * Check that when the alarm timer interrupt is enabled, that an interrupt
 * occurs
 */
static void check_alarm_interrupt_day_of_week(void)
{

    /* Set an alarm for midnight */
    uint8_t buf[3];

    buf[0] = bin2bcd(0); /* minutes */
    buf[1] = bin2bcd(0); /* hours */
    buf[2] = 0x01 << 2; /* day */

    write_registers(ALARM_MINUTE, buf, 3);

    /* Set alarm to day of week mode */
    clear_bits_in_register(EXTENSION_REGISTER, EXT_MASK_WADA);

    /* Enable the alarm interrupt */
    set_bits_in_register(CONTROL_REGISTER, CTRL_MASK_AIE);

    /* Wait for the clock to rollover */
    set_time(59, 59, 23, 1, 29, 2, 16);

    count_reset();
    wait_for(2 * 1000000, 1000);

    /* Disable the alarm interrupt */
    clear_bits_in_register(CONTROL_REGISTER, CTRL_MASK_AIE);

    g_assert_cmpuint(interrupt_counts[FLAG_REG_UF], ==, 0);
    g_assert_cmpuint(interrupt_counts[FLAG_REG_AF], ==, 1);
}

/**
 * Check that when the alarm timer interrupt is enabled, that an interrupt
 * does not occur
 */
static void check_alarm_interrupt_day_of_week_negative(void)
{

    /* Set an alarm for midnight */
    uint8_t buf[3];

    buf[0] = bin2bcd(0); /* minutes */
    buf[1] = bin2bcd(0); /* hours */
    buf[2] = 0x01 << 2; /* day */

    write_registers(ALARM_MINUTE, buf, 3);

    /* Set alarm to day of week mode */
    clear_bits_in_register(EXTENSION_REGISTER, EXT_MASK_WADA);

    /* Enable the alarm interrupt */
    set_bits_in_register(CONTROL_REGISTER, CTRL_MASK_AIE);

    /* Wait for the clock to rollover */
    set_time(59, 59, 23, 3, 29, 2, 16);

    count_reset();
    wait_for(2 * 1000000, 1000);

    /* Disable the alarm interrupt */
    clear_bits_in_register(CONTROL_REGISTER, CTRL_MASK_AIE);

    g_assert_cmpuint(interrupt_counts[FLAG_REG_UF], ==, 0);
    g_assert_cmpuint(interrupt_counts[FLAG_REG_AF], ==, 0);
}

/**
 * Check that the reset function
 */
static void check_reset(void)
{
    set_bits_in_register(FLAG_REGISTER, FLAG_MASK_UF);
    set_bits_in_register(CONTROL_REGISTER, CTRL_MASK_RESET);

    g_assert_cmpuint(read_register(FLAG_REGISTER), ==,
            0x00);
}

/**
 * Check that Fout operates at 1Hz
 */
static void check_fout_1hz(void)
{
    uint8_t ext_reg = read_register(EXTENSION_REGISTER);
    ext_reg |= EXT_MASK_FSEL1;
    ext_reg &= ~EXT_MASK_FSEL0;
    write_register(EXTENSION_REGISTER, ext_reg);

    /* Enable Fout */
    irq_set(RX8900_TEST_ID, RX8900_FOUT_ENABLE, 0, true);

    fout_count_reset();
    wait_cycles(2 * 1000000000ULL, 1000000);

    /* disable Fout */
    irq_set(RX8900_TEST_ID, RX8900_FOUT_ENABLE, 0, false);

    g_assert_cmpuint(fout_counts, ==, 2);
}

/**
 * Check that Fout operates at 1024Hz
 */
static void check_fout_1024hz(void)
{
    uint8_t ext_reg = read_register(EXTENSION_REGISTER);
    ext_reg |= EXT_MASK_FSEL0;
    ext_reg &= ~EXT_MASK_FSEL1;
    write_register(EXTENSION_REGISTER, ext_reg);

    /* Enable Fout */
    irq_set(RX8900_TEST_ID, RX8900_FOUT_ENABLE, 0, true);

    fout_count_reset();
    wait_cycles(2 * 1000000000ULL, 100000);

    /* disable Fout */
    irq_set(RX8900_TEST_ID, RX8900_FOUT_ENABLE, 0, false);

    g_assert_cmpuint(fout_counts, ==, 1024 * 2);
}

/**
 * Check that Fout operates at 32768Hz
 */
static void check_fout_32768hz(void)
{
    uint8_t ext_reg = read_register(EXTENSION_REGISTER);
    ext_reg &= ~EXT_MASK_FSEL0;
    ext_reg &= ~EXT_MASK_FSEL1;
    write_register(EXTENSION_REGISTER, ext_reg);

    /* Enable Fout */
    irq_set(RX8900_TEST_ID, RX8900_FOUT_ENABLE, 0, true);

    fout_count_reset();
    wait_cycles(2 * 1000000000ULL, 15000);

    /* disable Fout */
    irq_set(RX8900_TEST_ID, RX8900_FOUT_ENABLE, 0, false);

    /* There appears to be some rounding errors in the timer,
     * we'll tolerate it for now
     */
    g_assert_cmpuint(fout_counts, >=, 32768 * 2);
    g_assert_cmpuint(fout_counts, <=, 32768 * 2 + 4);
}

/**
 * Check the countdown timer operates at 1 Hz
 */
static void check_countdown_1hz(void)
{
    uint8_t ext_reg;

    write_register(TIMER_COUNTER_0, 5);
    write_register(TIMER_COUNTER_1, 0);

    ext_reg = read_register(EXTENSION_REGISTER);
    ext_reg &= ~EXT_MASK_TSEL1;
    ext_reg |= EXT_MASK_TSEL0;
    ext_reg |= EXT_MASK_TE;
    write_register(EXTENSION_REGISTER, ext_reg);
    set_bits_in_register(CONTROL_REGISTER, CTRL_MASK_TIE);

    count_reset();
    wait_cycles(5 * 1000000000ULL, 1000000);

    g_assert_cmpuint(interrupt_counts[FLAG_REG_TF], ==, 0);

    wait_cycles(1 * 1000000000ULL, 1000000);

    g_assert_cmpuint(interrupt_counts[FLAG_REG_TF], ==, 1);
}

/**
 * Check the countdown timer operates at 64 Hz
 */
static void check_countdown_64hz(void)
{
    uint8_t ext_reg;

    write_register(TIMER_COUNTER_0, 0x40);
    write_register(TIMER_COUNTER_1, 0x01); /* 5 * 64 */

    ext_reg = read_register(EXTENSION_REGISTER);
    ext_reg &= ~EXT_MASK_TSEL0;
    ext_reg &= ~EXT_MASK_TSEL1;
    ext_reg |= EXT_MASK_TE;
    write_register(EXTENSION_REGISTER, ext_reg);
    set_bits_in_register(CONTROL_REGISTER, CTRL_MASK_TIE);

    count_reset();
    wait_cycles(5 * 1000000000ULL, 1000000);

    g_assert_cmpuint(interrupt_counts[FLAG_REG_TF], ==, 0);

    wait_cycles(1 * 1000000000ULL, 1000000);

    g_assert_cmpuint(interrupt_counts[FLAG_REG_TF], ==, 1);
}

/**
 * Check the countdown timer operates at 4096 Hz
 */
static void check_countdown_4096hz(void)
{
    uint8_t ext_reg;

    write_register(TIMER_COUNTER_0, 0xFF);
    write_register(TIMER_COUNTER_1, 0x0F); /* 4095 */
    ext_reg = read_register(EXTENSION_REGISTER);
    ext_reg |= EXT_MASK_TSEL0;
    ext_reg |= EXT_MASK_TSEL1;
    ext_reg |= EXT_MASK_TE;
    write_register(EXTENSION_REGISTER, ext_reg);
    set_bits_in_register(CONTROL_REGISTER, CTRL_MASK_TIE);

    count_reset();
    wait_cycles(999755859ULL, 10000);

    g_assert_cmpuint(interrupt_counts[FLAG_REG_TF], ==, 0);

    wait_cycles(244141ULL, 10000);

    g_assert_cmpuint(interrupt_counts[FLAG_REG_TF], ==, 1);
}

/**
 * Check the countdown timer operates at 1 minute
 */
static void check_countdown_1m(void)
{
    uint8_t ext_reg;

    write_register(TIMER_COUNTER_0, 0x01);
    write_register(TIMER_COUNTER_1, 0x00);
    ext_reg = read_register(EXTENSION_REGISTER);
    ext_reg &= ~EXT_MASK_TSEL0;
    ext_reg |= EXT_MASK_TSEL1;
    ext_reg |= EXT_MASK_TE;
    write_register(EXTENSION_REGISTER, ext_reg);
    set_bits_in_register(CONTROL_REGISTER, CTRL_MASK_TIE);

    count_reset();
    wait_cycles(59 * 1000000000ULL, 100000);

    g_assert_cmpuint(interrupt_counts[FLAG_REG_TF], ==, 0);

    wait_cycles(1000000001LL, 100000);

    g_assert_cmpuint(interrupt_counts[FLAG_REG_TF], ==, 1);
}

/**
 * Check that the voltage can be altered via properties
 */
static void check_voltage(void)
{
    uint8_t flags = read_register(FLAG_REGISTER) &
            (FLAG_MASK_VDET | FLAG_MASK_VLF);

    /* start from a good state */
    g_assert_cmpuint(flags, == , 0x00);

    /* 1.9V triggers VDET but not VLF */
    qmp_rx8900_set_voltage(RX8900_TEST_ID, 1.9f);

    flags = read_register(FLAG_REGISTER) & (FLAG_MASK_VDET | FLAG_MASK_VLF);
    g_assert_cmpuint(flags, == , FLAG_MASK_VDET);

    /* Clearing the flag should reassert it as the voltage is still low */
    write_register(FLAG_REGISTER, 0x00);

    flags = read_register(FLAG_REGISTER) & (FLAG_MASK_VDET | FLAG_MASK_VLF);
    g_assert_cmpuint(flags, == , FLAG_MASK_VDET);

    /* Set the voltage to a good level, the low voltage flag should persist */
    qmp_rx8900_set_voltage(RX8900_TEST_ID, 3.3f);

    flags = read_register(FLAG_REGISTER) & (FLAG_MASK_VDET | FLAG_MASK_VLF);
    g_assert_cmpuint(flags, == , FLAG_MASK_VDET);

    /* We should be able to clear the flag with a good voltage */
    write_register(FLAG_REGISTER, 0x00);

    flags = read_register(FLAG_REGISTER) & (FLAG_MASK_VDET | FLAG_MASK_VLF);
    g_assert_cmpuint(flags, == , 0x00);


    /* 1.5V should trigger both VDET & VLF */
    qmp_rx8900_set_voltage(RX8900_TEST_ID, 1.5f);
    flags = read_register(FLAG_REGISTER) & (FLAG_MASK_VDET | FLAG_MASK_VLF);
    g_assert_cmpuint(flags, == , FLAG_MASK_VDET | FLAG_MASK_VLF);


    /* Clearing the flag should reassert it as the voltage is still low */
    write_register(FLAG_REGISTER, 0x00);

    flags = read_register(FLAG_REGISTER) & (FLAG_MASK_VDET | FLAG_MASK_VLF);
    g_assert_cmpuint(flags, == , FLAG_MASK_VDET | FLAG_MASK_VLF);

    /* Set the voltage to a good level, the low voltage flag should persist */
    qmp_rx8900_set_voltage(RX8900_TEST_ID, 3.3f);

    flags = read_register(FLAG_REGISTER) & (FLAG_MASK_VDET | FLAG_MASK_VLF);
    g_assert_cmpuint(flags, == , FLAG_MASK_VDET | FLAG_MASK_VLF);

    /* We should be able to clear the flag with a good voltage */
    write_register(FLAG_REGISTER, 0x00);

    flags = read_register(FLAG_REGISTER) & (FLAG_MASK_VDET | FLAG_MASK_VLF);
    g_assert_cmpuint(flags, == , 0);
}



int main(int argc, char **argv)
{
    QTestState *s = NULL;
    int ret;
    char args[255];
    snprintf(args, sizeof(args), "-display none -machine imx25-pdk "
            "-device rx8900,bus=i2c-bus.0,address=0x%x,id=%s"
#ifdef RX8900_TRACE
            " -trace events=/tmp/events"
#endif
            ,
            RX8900_ADDR, RX8900_TEST_ID);

    g_test_init(&argc, &argv, NULL);

    s = qtest_start(args);
    i2c = imx_i2c_create(IMX25_I2C_0_BASE);
    addr = RX8900_ADDR;

    irq_intercept_out(RX8900_TEST_ID);
    irq_attach(RX8900_INTERRUPT_OUT, 0, handle_interrupt, NULL);
    irq_attach(RX8900_FOUT, 0, handle_fout, NULL);

    qtest_add_func("/rx8900/reset", check_reset);
    qtest_add_func("/rx8900/tx-rx", send_and_receive);
    qtest_add_func("/rx8900/temperature", check_temperature);
    qtest_add_func("/rx8900/rollover", check_rollover);
    qtest_add_func("/rx8900/update-interrupt-disabled",
            check_update_interrupt_disabled);
    qtest_add_func("/rx8900/update-interrupt-seconds",
            check_update_interrupt_seconds);
    qtest_add_func("/rx8900/update-interrupt-minutes",
            check_update_interrupt_minutes);
    qtest_add_func("/rx8900/alarm-interrupt-disabled",
            check_alarm_interrupt_disabled);
    qtest_add_func("/rx8900/alarm-interrupt-month",
            check_alarm_interrupt_day_of_month);
    qtest_add_func("/rx8900/alarm-interrupt-month-negative",
            check_alarm_interrupt_day_of_month_negative);
    qtest_add_func("/rx8900/alarm-interrupt-week",
            check_alarm_interrupt_day_of_week);
    qtest_add_func("/rx8900/alarm-interrupt-week-negative",
            check_alarm_interrupt_day_of_week_negative);
    qtest_add_func("/rx8900/fout_1hz", check_fout_1hz);
    qtest_add_func("/rx8900/fout_1024hz", check_fout_1024hz);
    qtest_add_func("/rx8900/fout_32768hz", check_fout_32768hz);
    qtest_add_func("/rx8900/countdown_1hz", check_countdown_1hz);
    qtest_add_func("/rx8900/countdown_64hz", check_countdown_64hz);
    qtest_add_func("/rx8900/countdown_4096hz", check_countdown_4096hz);
    qtest_add_func("/rx8900/countdown_1m", check_countdown_1m);
    qtest_add_func("/rx8900/low_voltage", check_voltage);

    ret = g_test_run();

    if (s) {
        qtest_quit(s);
    }
    g_free(i2c);

    return ret;
}
