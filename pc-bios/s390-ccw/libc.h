/*
 * libc-style definitions and functions
 *
 * This code is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or (at your
 * option) any later version.
 */

#ifndef S390_CCW_LIBC_H
#define S390_CCW_LIBC_H

typedef long               size_t;
typedef int                bool;
typedef unsigned char      uint8_t;
typedef unsigned short     uint16_t;
typedef unsigned int       uint32_t;
typedef unsigned long long uint64_t;

static inline void *memset(void *s, int c, size_t n)
{
    int i;
    unsigned char *p = s;

    for (i = 0; i < n; i++) {
        p[i] = c;
    }

    return s;
}

static inline void *memcpy(void *s1, const void *s2, size_t n)
{
    uint8_t *dest = s1;
    const uint8_t *src = s2;
    int i;

    for (i = 0; i < n; i++) {
        dest[i] = src[i];
    }

    return s1;
}

static inline int memcmp(const void *s1, const void *s2, size_t n)
{
    int i;
    const uint8_t *p1 = s1, *p2 = s2;

    for (i = 0; i < n; i++) {
        if (p1[i] != p2[i]) {
            return p1[i] > p2[i] ? 1 : -1;
        }
    }

    return 0;
}

static inline size_t strlen(const char *str)
{
    size_t i;
    for (i = 0; *str; i++) {
        str++;
    }
    return i;
}

static inline int isdigit(int c)
{
    return (c >= '0') && (c <= '9');
}

/* atoi
 *
 * Given a string, convert it to an integer. Any
 * non-numerical value will end the conversion.
 *
 * @str - the string to be converted
 *
 * @return - an integer converted from the string str
 */
static inline int atoi(const char *str)
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

/* itostr
 *
 * Given an integer, convert it to a string. The string must be allocated
 * beforehand. The resulting string will be null terminated and returned.
 *
 * @str - the integer to be converted
 * @num - a pointer to a string to store the conversion
 *
 * @return - the string of the converted integer
 */
static inline char *itostr(int num, char *str)
{
    int i;
    int len = 0;
    long tens = 1;

    /* Handle 0 */
    if (num == 0) {
        str[0] = '0';
        str[1] = '\0';
        return str;
    }

    /* Count number of digits */
    while (num / tens != 0) {
        tens *= 10;
        len++;
    }

    /* Convert int -> string */
    for (i = 0; i < len; i++) {
        tens /= 10;
        str[i] = num / tens % 10 + '0';
    }

    str[i] = '\0';

    return str;
}

#endif
