/*
 * libc-style definitions and functions
 *
 * Copyright 2018 IBM Corp.
 * Author(s): Collin L. Walling <walling@linux.vnet.ibm.com>
 *
 * This code is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or (at your
 * option) any later version.
 */

#include "libc.h"
#include "s390-ccw.h"

/**
 * atoi:
 * @str: the string to be converted.
 *
 * Given a string @str, convert it to an integer. Leading whitespace is
 * ignored. The first character (after any whitespace) is checked for the
 * negative sign. Any other non-numerical value will terminate the
 * conversion.
 *
 * Returns: an integer converted from the string @str.
 */
int atoi(const char *str)
{
    int val = 0;
    int sign = 1;

    if (!str || !str[0]) {
        return 0;
    }

    while (*str == ' ') {
        str++;
    }

    if (*str == '-') {
        sign = -1;
        str++;
    }

    while (*str) {
        if (!isdigit(*str)) {
            break;
        }
        val = val * 10 + *str - '0';
        str++;
    }

    return val * sign;
}

/**
 * itostr:
 * @num: an integer (base 10) to be converted.
 * @str: a pointer to a string to store the conversion.
 * @len: the length of the passed string.
 *
 * Given an integer @num, convert it to a string. The string @str must be
 * allocated beforehand. The resulting string will be null terminated and
 * returned.
 *
 * Returns: the string @str of the converted integer @num; NULL if @str
 * is NULL or if there is not enough space allocated.
 */
static char *_itostr(int num, char *str, size_t len)
{
    int num_idx = 0;
    int tmp = num;
    char sign = 0;

    if (!str) {
        return NULL;
    }

    /* Get index to ones place */
    while ((tmp /= 10) != 0) {
        num_idx++;
    }

    if (num < 0) {
        num *= -1;
        sign = 1;
    }

    /* Check if we have enough space for num, sign, and null */
    if (len <= num_idx + sign + 1) {
        return NULL;
    }

    str[num_idx + sign + 1] = '\0';

    /* Convert int to string */
    while (num_idx >= 0) {
        str[num_idx + sign] = num % 10 + '0';
        num /= 10;
        num_idx--;
    }

    if (sign) {
        str[0] = '-';
    }

    return str;
}

char *itostr(int num, char *str, size_t len)
{
    char *tmp = _itostr(num, str, len);
    IPL_assert(str != NULL, "cannot convert NULL to int");
    IPL_assert(tmp != NULL, "array too small for integer to string conversion");
    return tmp;
}
