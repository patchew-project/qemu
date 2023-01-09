/*
 * Utility function around QEMU release version
 *
 * Copyright (c) 2023 Linaro Ltd
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef QEMU_UTIL_VERSION_H
#define QEMU_UTIL_VERSION_H

/**
 * qemu_version_delta - Return delta between two release versions ('A' and 'B').
 * @version_major_a: Version 'A' major number
 * @version_minor_a: Version 'A' minor number
 * @version_major_b: Version 'B' major number
 * @version_minor_b: Version 'B' minor number
 *
 * Returns a negative number is returned if 'A' is older than 'B', or positive
 * if 'A' is newer than 'B'. The number represents the number of minor versions.
 */
int qemu_version_delta(unsigned version_major_a, unsigned version_minor_a,
                       unsigned version_major_b, unsigned version_minor_b);

/**
 * qemu_version_delta_current - Return delta with current QEMU release version.
 * @version_major: The major version
 * @version_minor: The minor version
 *
 * Returns the number of minor versions between the current released
 * version and the requested $major.$minor. A negative number is returned
 * for older versions and positive for newer.
 */
int qemu_version_delta_current(unsigned version_major, unsigned version_minor);

#endif
