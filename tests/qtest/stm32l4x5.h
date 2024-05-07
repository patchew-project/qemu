/*
 * QTest testcase header for STM32L4X5 :
 * used for consolidating common objects in stm32l4x5_*-test.c
 *
 * Copyright (c) 2024 Arnaud Minier <arnaud.minier@telecom-paris.fr>
 * Copyright (c) 2024 Inès Varhol <ines.varhol@telecom-paris.fr>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"
#include "libqtest.h"

/*
 * MSI (4 MHz) is used as system clock source after startup
 * from Reset.
 * AHB, APB1 and APB2 prescalers are set to 1 at reset.
 */
#define SYSCLK_FREQ_HZ 4000000
#define RCC_AHB2ENR 0x4002104C
#define RCC_APB1ENR1 0x40021058
#define RCC_APB1ENR2 0x4002105C
#define RCC_APB2ENR 0x40021060


static inline uint32_t get_clock_freq_hz(QTestState *qts, const char *path)
{
    uint32_t clock_freq_hz = 0;
    QDict *r;

    r = qtest_qmp(qts, "{ 'execute': 'qom-get', 'arguments':"
        " { 'path': %s, 'property': 'clock-freq-hz'} }", path);
    g_assert_false(qdict_haskey(r, "error"));
    clock_freq_hz = qdict_get_int(r, "return");
    qobject_unref(r);
    return clock_freq_hz;
}


