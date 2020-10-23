/*
 * Decorators to disable sanitizers on specific functions
 *
 * Copyright IBM Corp., 2020
 *
 * Author:
 *  Daniele Buono <dbuono@linux.vnet.ibm.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 *
 */

#ifdef CONFIG_CFI
/* If CFI is enabled, use an attribute to disable cfi-icall on the following
 * function */
#define __disable_cfi__ __attribute__((no_sanitize("cfi-icall")))
#else
/* If CFI is not enabled, use an empty define to not change the behavior */
#define __disable_cfi__
#endif

