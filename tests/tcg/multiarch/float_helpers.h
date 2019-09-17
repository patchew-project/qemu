/*
 * Common Float Helpers
 *
 * Copyright (c) 2019 Linaro
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include <inttypes.h>

/* Number of constants in each table */
int get_num_f16();
int get_num_f32();
int get_num_f64();

/* Add additional constants */
void add_f16_const(uint16_t);
void add_f32_const(float);
void add_f64_const(double);

/* Accessor helpers */
uint16_t get_f16(int i); /* use _Float16 when we can */
float    get_f32(int i);
double   get_f64(int i);

/* Return format strings, free after use */
char * fmt_f16(uint16_t);
char * fmt_f32(float);
char * fmt_f64(double);
/* exception flags */
char * fmt_flags(void);
