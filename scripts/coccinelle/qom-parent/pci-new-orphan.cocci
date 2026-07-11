// SPDX-License-Identifier: GPL-2.0-or-later
//
// Rename pci_new*/pci_create_simple*() to *_orphan() so that the
// short names can be reintroduced with a mandatory (parent, id, ...)
// signature.
//
// spatch --sp-file scripts/coccinelle/qom-parent/pci-new-orphan.cocci \
//        --in-place --include-headers --dir .

@@
@@
- pci_new_multifunction
+ pci_new_multifunction_orphan

@@
@@
- pci_new
+ pci_new_orphan

@@
@@
- pci_create_simple_multifunction
+ pci_create_simple_multifunction_orphan

@@
@@
- pci_create_simple
+ pci_create_simple_orphan
