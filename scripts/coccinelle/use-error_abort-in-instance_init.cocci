// Use &error_abort in TypeInfo::instance_init()
//
// Copyright: (C) 2020 Philippe Mathieu-Daudé
// This work is licensed under the terms of the GNU GPLv2 or later.
//
// spatch \
//  --macro-file scripts/cocci-macro-file.h --include-headers \
//  --sp-file scripts/coccinelle/use-error_abort-in-instance_init.cocci \
//  --keep-comments --in-place
//
// Inspired by https://www.mail-archive.com/qemu-devel@nongnu.org/msg692500.html
// and https://www.mail-archive.com/qemu-devel@nongnu.org/msg693637.html


@ has_qapi_error @
@@
    #include "qapi/error.h"


@ match_instance_init @
TypeInfo info;
identifier instance_initfn;
@@
    info.instance_init = instance_initfn;


@ use_error_abort @
identifier match_instance_init.instance_initfn;
identifier func_with_error;
expression parentobj, propname, childobj, size, type, errp;
position pos;
@@
void instance_initfn(...)
{
   <+...
(
   object_initialize_child(parentobj, propname,
                           childobj, size, type,
                           errp, NULL);
|
   func_with_error@pos(...,
-                           NULL);
+                           &error_abort);
)
   ...+>
}


@script:python depends on use_error_abort && !has_qapi_error@
p << use_error_abort.pos;
@@
print('[[manual edit required, %s misses #include "qapi/error.h"]]' % p[0].file)
