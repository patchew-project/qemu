// SPDX-License-Identifier: GPL-2.0-or-later
//
// Rename qdev_new()/qdev_try_new() to qdev_new_orphan()/qdev_try_new_orphan()
// so that the short name can be reintroduced with a mandatory
// (parent, id, type) signature.
//
// spatch --sp-file scripts/coccinelle/qom-parent/qdev-new-orphan.cocci \
//        --in-place --include-headers --dir .

@@
@@
- qdev_new
+ qdev_new_orphan

@@
@@
- qdev_try_new
+ qdev_try_new_orphan
