// Add missing error-propagation code
//
// Copyright: (C) 2020 Philippe Mathieu-Daudé.
// This work is licensed under the terms of the GNU GPLv2 or later.
//
// spatch \
//  --macro-file scripts/cocci-macro-file.h --include-headers \
//  --sp-file scripts/coccinelle/object_property_missing_error_propagate.cocci \
//  --keep-comments --smpl-spacing --in-place --dir hw
//
// Inspired by https://www.mail-archive.com/qemu-devel@nongnu.org/msg691638.html

@match exists@
typedef Error;
Error *err;
identifier func, errp;
identifier object_property_set_type1 =~ "^object_property_set_.*";
identifier object_property_set_type2 =~ "^object_property_set_.*";
expression obj;
@@
void func(..., Error **errp)
{
 <+...
 object_property_set_type1(obj, ..., &err);
 ... when != err
 object_property_set_type2(obj, ..., &err);
 ...+>
}

@@
Error *match.err;
identifier match.errp;
identifier match.object_property_set_type1;
expression match.obj;
@@
 object_property_set_type1(obj, ..., &err);
+if (err) {
+    error_propagate(errp, err);
+    return;
+}

@manual depends on never match@
Error *err;
identifier object_property_set_type1 =~ "^object_property_set_.*";
identifier object_property_set_type2 =~ "^object_property_set_.*";
position p;
@@
 object_property_set_type1@p(..., &err);
 ... when != err
 object_property_set_type2(..., &err);

@script:python@
f << manual.object_property_set_type1;
p << manual.p;
@@
print("[[manual check required: "
      "error_propagate() might be missing in {}() {}:{}:{}]]".format(
            f, p[0].file, p[0].line, p[0].column))
