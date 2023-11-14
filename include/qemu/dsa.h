#ifndef QEMU_DSA_H
#define QEMU_DSA_H

#include "qemu/thread.h"
#include "qemu/queue.h"

#ifdef CONFIG_DSA_OPT

#pragma GCC push_options
#pragma GCC target("enqcmd")

#include <linux/idxd.h>
#include "x86intrin.h"

#endif

/**
 * @brief Initializes DSA devices.
 *
 * @param dsa_parameter A list of DSA device path from migration parameter.
 * @return int Zero if successful, otherwise non zero.
 */
int dsa_init(const char *dsa_parameter);

/**
 * @brief Start logic to enable using DSA.
 */
void dsa_start(void);

/**
 * @brief Stop logic to clean up DSA by halting the device group and cleaning up
 * the completion thread.
 */
void dsa_stop(void);

/**
 * @brief Clean up system resources created for DSA offloading.
 *        This function is called during QEMU process teardown.
 */
void dsa_cleanup(void);

/**
 * @brief Check if DSA is running.
 *
 * @return True if DSA is running, otherwise false.
 */
bool dsa_is_running(void);

#endif