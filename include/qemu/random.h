/*
 * QEMU random functions
 *
 * Copyright 2019 Linaro, Ltd.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 2 of the License, or (at your option)
 * any later version.
 */

#ifndef QEMU_RANDOM_H
#define QEMU_RANDOM_H

/**
 * qemu_seedrandom_main(const char *optarg, Error **errp)
 * @optarg: a non-NULL pointer to a C string
 * @errp: an Error handler
 *
 * The @optarg value is that which accompanies the -seed argument.
 * This forces qemu_getrandom into deterministic mode.
 */
void qemu_seedrandom_main(const char *optarg, Error **errp);

/**
 * qemu_seedrandom_thread_part1(void)
 *
 * If qemu_getrandom is in deterministic mode, returns an
 * independant seed for the new thread.  Otherwise returns 0.
 */
uint64_t qemu_seedrandom_thread_part1(void);

/**
 * qemu_seedrandom_thread_part2(uint64_t seed)
 * @seed: a value for the new thread.
 *
 * If qemu_getrandom is in deterministic mode, this stores an
 * independant seed for the new thread.  Otherwise a no-op.
 */
void qemu_seedrandom_thread_part2(uint64_t seed);

/**
 * qemu_getrandom(void *buf, size_t len, bool nonblock)
 * @buf: a buffer of bytes to be written
 * @len: the number of bytes in @buf
 * @nonblock: do not delay if the entropy pool is low
 *
 * Fills len bytes in buf with random data.  If nonblock is false,
 * this may require a delay while the entropy pool fills.  Returns
 * true if the call is successful, but the only non-successful case
 * is when nonblock is true.
 *
 * The value of len must be <= 256, so that the BSD getentropy(3)
 * function can be used to implement this.
 */
bool qemu_getrandom(void *buf, size_t len, bool nonblock);

#endif /* QEMU_RANDOM_H */
