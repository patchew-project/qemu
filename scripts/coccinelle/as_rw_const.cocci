// Avoid uses of address_space_rw() with a constant is_write argument.
// Usage:
//  spatch --sp-file as-rw-const.spatch --dir . --in-place

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
