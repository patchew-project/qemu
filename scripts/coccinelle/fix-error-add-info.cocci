@rule0@
// Add invocation to errp-functions
identifier fn;
@@

 fn(..., Error **errp, ...)
 {
+   ERRP_AUTO_PROPAGATE();
    <+...
(
    error_append_hint(errp, ...);
|
    error_prepend(errp, ...);
)
    ...+>
 }

@@
// Drop doubled invocation
identifier rule0.fn;
@@

 fn(...)
{
    ERRP_AUTO_PROPAGATE();
-   ERRP_AUTO_PROPAGATE();
    ...
}
