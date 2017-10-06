/* transform g_new*() alloc with size arguments of the form sizeof(T) [* N]
 *
 *   g_new(T, n) is neater than g_malloc(sizeof(T) * n).  It's also safer,
 *   for two reasons.  One, it catches multiplication overflowing size_t.
 *   two, it returns T * rather than void *, which lets the compiler catch
 *   more type errors.
 *
 *    Copyright: (C) 2017 Markus Armbruster <armbru@redhat.com>. GPLv2+.
 *    (Imported from b45c03f585ea9bb1af76c73e82195418c294919d)
 *
 * See http://lists.nongnu.org/archive/html/qemu-devel/2017-09/msg00908.html:
 *
 *   g_new() advantages (from glib doc):
 *   - the returned pointer is cast to a pointer to the given type.
 *   - care is taken to avoid overflow when calculating the size of the
 *   allocated block.
 *
 *   p = g_malloc(sizeof(*p)) is idiomatic, and not obviously inferior to
 *   p = g_new(T, 1), where T is the type of *p.
 *   But once you add multiplication, g_new() adds something useful: overflow
 *   protection.  Conversion to g_new() might make sense then.
 */

@@
type T;
@@
-g_malloc(sizeof(T))
+g_new(T, 1)
@@
type T;
@@
-g_try_malloc(sizeof(T))
+g_try_new(T, 1)
@@
type T;
@@
-g_malloc0(sizeof(T))
+g_new0(T, 1)
@@
type T;
@@
-g_try_malloc0(sizeof(T))
+g_try_new0(T, 1)

@@
type T;
expression n;
@@
-g_malloc(sizeof(T) * (n))
+g_new(T, n)
@@
type T;
expression n;
@@
-g_try_malloc(sizeof(T) * (n))
+g_try_new(T, n)
@@
type T;
expression n;
@@
-g_malloc0(sizeof(T) * (n))
+g_new0(T, n)
@@
type T;
expression n;
@@
-g_try_malloc0(sizeof(T) * (n))
+g_try_new0(T, n)

@@
type T;
expression p, n;
@@
-g_realloc(p, sizeof(T) * (n))
+g_renew(T, p, n)
@@
type T;
expression p, n;
@@
-g_try_realloc(p, sizeof(T) * (n))
+g_try_renew(T, p, n)

@@
type T;
expression n;
@@
(
-g_malloc_n(n, sizeof(T))
+g_new(T, n)
|
-g_malloc0_n(n, sizeof(T))
+g_new0(T, n)
|
-g_try_malloc_n(n, sizeof(T))
+g_try_new(T, n)
|
-g_try_malloc0_n(n, sizeof(T))
+g_try_new0(T, n)
)

@@
type T;
identifier m;
@@
T *m;
...
(
-m = g_malloc(sizeof(*m));
+m = g_new(T, 1);
|
-m = g_malloc0(sizeof(*m));
+m = g_new0(T, 1);
|
-m = g_try_malloc(sizeof(*m));
+m = g_try_new(T, 1);
|
-m = g_try_malloc0(sizeof(*m));
+m = g_try_new0(T, 1);
)

@@
type T;
identifier m;
@@
T **m;
...
- *m = g_malloc0(sizeof(**m));
+ *m = g_new0(T *, 1);

////////////////////////////////////////
//
// last transformations: cleanups
//

// drop superfluous cast
@@
type T;
expression n;
@@
-(T *)g_new(T, n)
+g_new(T, n)
@@
type T;
expression n;
@@
-(T *)g_new0(T, n)
+g_new0(T, n)
@@
type T;
expression p, n;
@@
-(T *)g_renew(T, p, n)
+g_renew(T, p, n)
@@
type T;
expression n;
@@
(
-(T *)g_try_new(T, n)
+g_try_new(T, n)
|
-(T *)g_try_new0(T, n)
+g_try_new0(T, n)
)

// drop superfluous parenthesis
@@
type T;
expression c;
@@
(
-g_new(T, (c))
+g_new(T, c)
|
-g_try_new(T, (c))
+g_try_new(T, c)
|
-g_new0(T, (c))
+g_new0(T, c)
|
-g_try_new0(T, (c))
+g_try_new0(T, c)
)
