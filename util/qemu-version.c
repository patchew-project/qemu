/*
 * Utility function around QEMU release version
 *
 * Copyright (c) 2023 Linaro Ltd
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "qemu/qemu-version.h"
#include "config-host.h"

#define QEMU_FIRST_MAJOR_VERSION_SUPPORTED 4
#define QEMU_MINOR_VERSIONS_PER_MAJOR 3

int qemu_version_delta(unsigned version_major_a, unsigned version_minor_a,
                       unsigned version_major_b, unsigned version_minor_b)
{
    int delta;

    assert(version_major_a >= QEMU_FIRST_MAJOR_VERSION_SUPPORTED);
    assert(version_major_b >= QEMU_FIRST_MAJOR_VERSION_SUPPORTED);
    assert(version_minor_a < QEMU_MINOR_VERSIONS_PER_MAJOR);
    assert(version_minor_b < QEMU_MINOR_VERSIONS_PER_MAJOR);

    delta = version_major_b - version_major_a;
    delta *= QEMU_MINOR_VERSIONS_PER_MAJOR;
    delta += version_minor_b - version_minor_a;

    return delta;
}

int qemu_version_delta_current(unsigned version_major, unsigned version_minor)
{
    return qemu_version_delta(QEMU_VERSION_MAJOR, QEMU_VERSION_MINOR,
                              version_major, version_minor);
}
