#ifndef CHAR_SERIAL_H
#define CHAR_SERIAL_H

#ifdef _WIN32
#define HAVE_CHARDEV_SERIAL 1
#elif defined(__linux__) || defined(__sun__) || defined(__FreeBSD__)    \
    || defined(__NetBSD__) || defined(__OpenBSD__) || defined(__DragonFly__) \
    || defined(__GLIBC__)
#define HAVE_CHARDEV_SERIAL 1
#endif

#endif
