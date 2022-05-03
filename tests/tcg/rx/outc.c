/*
 * minilib.h compatibility code
 *
 * Copyright Linaro Ltd 2022
 *
 * Rely on newlib/libgloss for functionality.
 */

#include "minilib.h"
#include <sys/unistd.h>

void __sys_outc(char c)
{
    write(1, &c, 1);
}
