// SPDX-License-Identifier: GPL-2.0-or-later
//
// Rename ssi_create_peripheral()/usb_create_simple() to *_orphan()
// so that the short names can be reintroduced with a mandatory
// (parent, id, ...) signature.
//
// spatch --sp-file scripts/coccinelle/qom-parent/ssi-usb-orphan.cocci \
//        --in-place --include-headers --dir .

@@
@@
- ssi_create_peripheral
+ ssi_create_peripheral_orphan

@@
@@
- usb_create_simple
+ usb_create_simple_orphan
