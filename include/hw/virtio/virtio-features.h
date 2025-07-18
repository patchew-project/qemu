/*
 * Virtio features helpers
 *
 * Copyright 2025 Red Hat, Inc.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef QEMU_VIRTIO_FEATURES_H
#define QEMU_VIRTIO_FEATURES_H

#include "qemu/bitops.h"

#define VIRTIO_FEATURES_FMT        "%016"PRIx64"%016"PRIx64
#define VIRTIO_FEATURES_PR(f)      (f)[1], (f)[0]

#define VIRTIO_FEATURES_MAX        128
#define VIRTIO_BIT(b)              BIT_ULL((b) % 64)
#define VIRTIO_DWORD(b)            ((b) >> 6)
#define VIRTIO_FEATURES_WORDS      (VIRTIO_FEATURES_MAX >> 5)
#define VIRTIO_FEATURES_DWORDS     (VIRTIO_FEATURES_WORDS >> 1)

#define VIRTIO_DECLARE_FEATURES(name)                        \
    union {                                                  \
        uint64_t name;                                       \
        uint64_t name##_ex[VIRTIO_FEATURES_DWORDS];          \
    }

static inline void virtio_features_clear(uint64_t *features)
{
    memset(features, 0, sizeof(features[0]) * VIRTIO_FEATURES_DWORDS);
}

static inline void virtio_features_from_u64(uint64_t *features, uint64_t from)
{
    virtio_features_clear(features);
    features[0] = from;
}

static inline bool virtio_has_feature_ex(const uint64_t *features,
                                         unsigned int fbit)
{
    assert(fbit < VIRTIO_FEATURES_MAX);
    return features[VIRTIO_DWORD(fbit)] & VIRTIO_BIT(fbit);
}

static inline void virtio_add_feature_ex(uint64_t *features,
                                         unsigned int fbit)
{
    assert(fbit < VIRTIO_FEATURES_MAX);
    features[VIRTIO_DWORD(fbit)] |= VIRTIO_BIT(fbit);
}

static inline void virtio_clear_feature_ex(uint64_t *features,
                                           unsigned int fbit)
{
    assert(fbit < VIRTIO_FEATURES_MAX);
    features[VIRTIO_DWORD(fbit)] &= ~VIRTIO_BIT(fbit);
}

static inline bool virtio_features_equal(const uint64_t *f1,
                                         const uint64_t *f2)
{
    return !memcmp(f1, f2, sizeof(uint64_t) * VIRTIO_FEATURES_DWORDS);
}

static inline bool virtio_features_use_extended(const uint64_t *features)
{
    int i;

    for (i = 1; i < VIRTIO_FEATURES_DWORDS; ++i) {
        if (features[i]) {
            return true;
        }
    }
    return false;
}

static inline bool virtio_features_empty(const uint64_t *features)
{
    return !virtio_features_use_extended(features) && !features[0];
}

static inline void virtio_features_copy(uint64_t *to, const uint64_t *from)
{
    memcpy(to, from, sizeof(to[0]) * VIRTIO_FEATURES_DWORDS);
}

static inline bool virtio_features_andnot(uint64_t *to, const uint64_t *f1,
                                           const uint64_t *f2)
{
    uint64_t diff = 0;
    int i;

    for (i = 0; i < VIRTIO_FEATURES_DWORDS; i++) {
        to[i] = f1[i] & ~f2[i];
        diff |= to[i];
    }
    return diff;
}

static inline void virtio_features_and(uint64_t *to, const uint64_t *f1,
                                       const uint64_t *f2)
{
    int i;

    for (i = 0; i < VIRTIO_FEATURES_DWORDS; i++) {
        to[i] = f1[i] & f2[i];
    }
}

static inline void virtio_features_or(uint64_t *to, const uint64_t *f1,
                                       const uint64_t *f2)
{
    int i;

    for (i = 0; i < VIRTIO_FEATURES_DWORDS; i++) {
        to[i] = f1[i] | f2[i];
    }
}

#endif

