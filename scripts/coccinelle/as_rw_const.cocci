// Avoid uses of address_space_rw() with a constant is_write argument.
// Usage:
//  spatch --sp-file scripts/coccinelle/as_rw_const.cocci --dir . --in-place

@@
expression E1, E2, E3, E4, E5;
symbol false;
@@

- address_space_rw(E1, E2, E3, E4, E5, false)
+ address_space_read(E1, E2, E3, E4, E5)
@@
expression E1, E2, E3, E4, E5;
@@

- address_space_rw(E1, E2, E3, E4, E5, 0)
+ address_space_read(E1, E2, E3, E4, E5)
@@
expression E1, E2, E3, E4, E5;
symbol true;
@@

- address_space_rw(E1, E2, E3, E4, E5, true)
+ address_space_write(E1, E2, E3, E4, E5)
@@
expression E1, E2, E3, E4, E5;
@@

- address_space_rw(E1, E2, E3, E4, E5, 1)
+ address_space_write(E1, E2, E3, E4, E5)

// Avoid uses of cpu_physical_memory_rw() with a constant is_write argument.
@@
expression E1, E2, E3;
@@
(
- cpu_physical_memory_rw(E1, E2, E3, false)
+ cpu_physical_memory_read(E1, E2, E3)
|
- cpu_physical_memory_rw(E1, E2, E3, 0)
+ cpu_physical_memory_read(E1, E2, E3)
|
- cpu_physical_memory_rw(E1, E2, E3, true)
+ cpu_physical_memory_write(E1, E2, E3)
|
- cpu_physical_memory_rw(E1, E2, E3, 1)
+ cpu_physical_memory_write(E1, E2, E3)
)
