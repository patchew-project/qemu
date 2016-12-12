#ifndef CHAR_PARALLEL_H
#define CHAR_PARALLEL_H

#if defined(__linux__) || defined(__FreeBSD__) || \
    defined(__FreeBSD_kernel__) || defined(__DragonFly__)
#define HAVE_CHARDEV_PARPORT 1
#endif

#endif /* CHAR_PARALLEL_H */
