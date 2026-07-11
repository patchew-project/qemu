// Rename the IRQ allocation helpers to *_orphan() so the parented
// variants can take the well-known names.
@@ @@
- qemu_allocate_irqs
+ qemu_allocate_irqs_orphan
@@ @@
- qemu_allocate_irq
+ qemu_allocate_irq_orphan
@@ @@
- qemu_extend_irqs
+ qemu_extend_irqs_orphan
@@ @@
- qemu_irq_invert
+ qemu_irq_invert_orphan
