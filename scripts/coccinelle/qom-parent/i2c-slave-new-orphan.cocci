// SPDX-License-Identifier: GPL-2.0-or-later
//
// Rename i2c_slave_new()/i2c_slave_create_simple() to *_orphan()
// so that the short names can be reintroduced with a mandatory
// (parent, id, ...) signature.
//
// spatch --sp-file scripts/coccinelle/qom-parent/i2c-slave-new-orphan.cocci \
//        --in-place --include-headers --dir .

@@
@@
- i2c_slave_new
+ i2c_slave_new_orphan

@@
@@
- i2c_slave_create_simple
+ i2c_slave_create_simple_orphan
