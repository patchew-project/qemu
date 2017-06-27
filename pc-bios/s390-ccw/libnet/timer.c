/*
 * Timer functions for libnet
 *
 * Copyright 2017 Thomas Huth, Red Hat Inc.
 *
 * This code is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or (at your
 * option) any later version.
 */

#include <stdint.h>
#include "time.h"

static uint64_t dest_timer;

static uint64_t get_timer_ms(void)
{
	uint64_t clk;

	asm volatile(" stck %0 " : : "Q"(clk) : "memory");

	/* Bit 51 is incrememented each microsecond */
	return (clk >> (63 - 51)) / 1000;
}

void set_timer(int val)
{
	dest_timer = get_timer_ms() + val;
}

int get_timer(void)
{
	return dest_timer - get_timer_ms();
}

int get_sec_ticks(void)
{
	return 1000;    /* number of ticks in 1 second */
}
