// Find error-propagation calls that don't need to be in DeviceClass::realize()
// because they don't use information user can change before calling realize(),
// so they can be moved to DeviceClass:initfn() where error propagation is not
// needed.
//
// Copyright: (C) 2020 Philippe Mathieu-Daudé
// This work is licensed under the terms of the GNU GPLv2 or later.
//
// spatch \
//  --macro-file scripts/cocci-macro-file.h \
//  --sp-file \
//    scripts/coccinelle/simplify-init-realize-error_propagate.cocci \
//  --timeout 60
//
// Inspired by https://www.mail-archive.com/qemu-devel@nongnu.org/msg692500.html


@ match_class_init @
TypeInfo info;
identifier class_initfn;
@@
    info.class_init = class_initfn;


@ match_instance_init @
TypeInfo info;
identifier instance_initfn;
@@
    info.instance_init = instance_initfn;


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
identifier func_with_errp =~ "(?!object_property_set_link)";
symbol error_abort, error_fatal;
position pos;
@@
void realizefn@pos(..., Error **errp)
{
    ...
    Error *err = NULL;
    <+...
    func_with_errp(..., \(&error_abort\|&error_fatal\));
    ...+>
}


@ script:python @
initfn << match_instance_init.instance_initfn;
realizefn << match_realize.realizefn;
p << propagate_in_realize.pos;
@@
print('>>> possible moves from {}() to {}() in {}:{}'
      .format(initfn, realizefn, p[0].file, p[0].line))
