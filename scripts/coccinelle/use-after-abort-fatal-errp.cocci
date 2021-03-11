/* Find dubious code use after error_abort/error_fatal
 *
 * Inspired by this patch:
 * https://www.mail-archive.com/qemu-devel@nongnu.org/msg789501.html
 *
 * Copyright (C) 2121 Red Hat, Inc.
 *
 * Authors:
 *  Philippe Mathieu-Daudé
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

@@
identifier func_with_errp;
@@
(
 if (func_with_errp(..., &error_fatal)) {
    /* Used for displaying help message */
    ...
    exit(...);
  }
|
*if (func_with_errp(..., &error_fatal)) {
    /* dubious code */
    ...
  }
|
*if (func_with_errp(..., &error_abort)) {
    /* dubious code */
    ...
  }
)
