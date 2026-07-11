// SPDX-License-Identifier: GPL-2.0-or-later
//
// Rename sysbus_create_simple()/sysbus_create_varargs() to *_orphan()
// so that the short names can be reintroduced with a mandatory
// (parent, id, ...) signature.
//
// spatch --sp-file scripts/coccinelle/qom-parent/sysbus-create-orphan.cocci \
//        --in-place --include-headers --dir .

@@
@@
- sysbus_create_simple
+ sysbus_create_simple_orphan

@@
@@
- sysbus_create_varargs
+ sysbus_create_varargs_orphan
