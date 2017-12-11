/*
 * libc-style definitions and functions
 *
 * This code is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or (at your
 * option) any later version.
 */

#include "libc.h"

/**
 * atoi:
 * @str: the string to be converted.
 *
 * Given a string @str, convert it to an integer. Any non-numerical value
 * will terminate the conversion.
 *
 * Returns: an integer converted from the string @str.
 */
int atoi(const char *str)
{
    int i;
    int val = 0;

    for (i = 0; str[i]; i++) {
        char c = str[i];
        if (!isdigit(c)) {
            break;
        }
        val *= 10;
        val += c - '0';
    }

    return val;
}

/**
 * itostr:
 * @num: the integer to be converted.
 * @str: a pointer to a string to store the conversion.
 * @len: the length of the passed string.
 *
 * Given an integer @num, convert it to a string. The string @str must be
 * allocated beforehand. The resulting string will be null terminated and
 * returned.
 *
 * Returns: the string @str of the converted integer @num.
 */
char *itostr(int num, char *str, size_t len)
{
    long num_len = 1;
    int tmp = num;
    int i;

    /* Count length of num */
    while ((tmp /= 10) > 0) {
        num_len++;
    }

    /* Check if we have enough space for num and null */
    if (len < num_len) {
        return 0;
    }

    /* Convert int to string */
    for (i = num_len - 1; i >= 0; i--) {
        str[i] = num % 10 + '0';
        num /= 10;
    }

    str[num_len] = '\0';

    return str;
}
