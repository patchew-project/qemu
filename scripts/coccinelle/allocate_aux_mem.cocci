// Replace by-hand memory_region_init_ram/vmstate_register_ram_global
// code sequences with use of the new memory_region_allocate_aux_memory
// utility function.

@@
expression MR;
expression OWNER;
expression NAME;
expression SIZE;
@@
-memory_region_init_ram(MR, OWNER, NAME, SIZE, &error_fatal);
+memory_region_allocate_aux_memory(MR, OWNER, NAME, SIZE);
 ...
-vmstate_register_ram_global(MR);
