// Add missing error-propagation code in DeviceClass::realize()
//
// Copyright: (C) 2020 Philippe Mathieu-Daudé
// This work is licensed under the terms of the GNU GPLv2 or later.
//
// spatch \
//  --macro-file scripts/cocci-macro-file.h --include-headers \
//  --sp-file scripts/coccinelle/use-error_abort-in-instance_init.cocci \
//  --keep-comments --timeout 60 --in-place
//
// Inspired by https://www.mail-archive.com/qemu-devel@nongnu.org/msg691638.html


@ match_class_init @
TypeInfo info;
identifier class_initfn;
@@
    info.class_init = class_initfn;


@ match_realize @
identifier match_class_init.class_initfn;
DeviceClass *dc;
identifier realizefn;
@@
void class_initfn(...)
{
    ...
    dc->realize = realizefn;
    ...
}


@ propagate_in_realize @
identifier match_realize.realizefn;
identifier err;
identifier errp;
identifier func_with_errp;
symbol error_abort, error_fatal;
@@
void realizefn(..., Error **errp)
{
    ...
    Error *err = NULL;
    <+...
    func_with_errp(...,
-                      \(&error_abort\|&error_fatal\));
+                      &err);
+   if (err) {
+       error_propagate(errp, err);
+       return;
+   }
    ...+>
}
