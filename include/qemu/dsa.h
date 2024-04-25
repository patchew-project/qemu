#ifndef QEMU_DSA_H
#define QEMU_DSA_H

#include "qemu/error-report.h"
#include "qemu/thread.h"
#include "qemu/queue.h"

#ifdef CONFIG_DSA_OPT

#pragma GCC push_options
#pragma GCC target("enqcmd")

#include <linux/idxd.h>
#include "x86intrin.h"

/**
 * @brief Initializes DSA devices.
 *
 * @param dsa_parameter A list of DSA device path from migration parameter.
 *
 * @return int Zero if successful, otherwise non zero.
 */
int dsa_init(const char *dsa_parameter);

/**
 * @brief Start logic to enable using DSA.
 */
void dsa_start(void);

/**
 * @brief Stop the device group and the completion thread.
 */
void dsa_stop(void);

/**
 * @brief Clean up system resources created for DSA offloading.
 */
void dsa_cleanup(void);

/**
 * @brief Check if DSA is running.
 *
 * @return True if DSA is running, otherwise false.
 */
bool dsa_is_running(void);

#else

static inline bool dsa_is_running(void)
{
    return false;
}

static inline int dsa_init(const char *dsa_parameter)
{
    if (dsa_parameter != NULL && strlen(dsa_parameter) != 0) {
        error_report("DSA not supported.");
        return -1;
    }

    return 0;
}

static inline void dsa_start(void) {}

static inline void dsa_stop(void) {}

static inline void dsa_cleanup(void) {}

#endif

#endif
