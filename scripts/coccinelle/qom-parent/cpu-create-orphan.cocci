// SPDX-License-Identifier: GPL-2.0-or-later
//
// Rename cpu_create() to cpu_create_orphan() so that the short
// name can be reintroduced with a mandatory (parent, id, type)
// signature.
//
// spatch --sp-file scripts/coccinelle/qom-parent/cpu-create-orphan.cocci \
//        --in-place --include-headers --dir .

@@
@@
- cpu_create
+ cpu_create_orphan
