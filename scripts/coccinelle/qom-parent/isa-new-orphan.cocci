// SPDX-License-Identifier: GPL-2.0-or-later
//
// Rename isa_new()/isa_try_new()/isa_create_simple() to *_orphan()
// so that the short names can be reintroduced with a mandatory
// (parent, id, ...) signature.
//
// spatch --sp-file scripts/coccinelle/qom-parent/isa-new-orphan.cocci \
//        --in-place --include-headers --dir .

@@
@@
- isa_new
+ isa_new_orphan

@@
@@
- isa_try_new
+ isa_try_new_orphan

@@
@@
- isa_create_simple
+ isa_create_simple_orphan
