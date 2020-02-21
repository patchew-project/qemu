/*
  Usage:

    spatch \
        --macro-file scripts/cocci-macro-file.h \
        --sp-file scripts/coccinelle/memory_region_owner_nonnull.cocci \
        --keep-comments \
        --in-place \
        --dir .

*/

// Device is owner
@@
typedef DeviceState;
identifier device_fn, dev, obj;
expression E1, E2, E3, E4, E5;
@@
static void device_fn(DeviceState *dev, ...)
{
  ...
  Object *obj = OBJECT(dev);
  <+...
(
- memory_region_init(E1, NULL, E2, E3);
+ memory_region_init(E1, obj, E2, E3);
|
- memory_region_init_io(E1, NULL, E2, E3, E4, E5);
+ memory_region_init_io(E1, obj, E2, E3, E4, E5);
|
- memory_region_init_alias(E1, NULL, E2, E3, E4, E5);
+ memory_region_init_alias(E1, obj, E2, E3, E4, E5);
|
- memory_region_init_rom(E1, NULL, E2, E3, E4);
+ memory_region_init_rom(E1, obj, E2, E3, E4);
|
- memory_region_init_ram(E1, NULL, E2, E3, E4);
+ memory_region_init_ram(E1, obj, E2, E3, E4);
|
- memory_region_init_ram_ptr(E1, NULL, E2, E3, E4);
+ memory_region_init_ram_ptr(E1, obj, E2, E3, E4);
|
- memory_region_init_ram_shared_nomigrate(E1, NULL, E2, E3, E4, E5);
+ memory_region_init_ram_shared_nomigrate(E1, obj, E2, E3, E4, E5);
)
  ...+>
}

// Device is owner
@@
identifier device_fn, dev;
expression E1, E2, E3, E4, E5;
@@
static void device_fn(DeviceState *dev, ...)
{
  <+...
(
- memory_region_init(E1, NULL, E2, E3);
+ memory_region_init(E1, OBJECT(dev), E2, E3);
|
- memory_region_init_io(E1, NULL, E2, E3, E4, E5);
+ memory_region_init_io(E1, OBJECT(dev), E2, E3, E4, E5);
|
- memory_region_init_alias(E1, NULL, E2, E3, E4, E5);
+ memory_region_init_alias(E1, OBJECT(dev), E2, E3, E4, E5);
|
- memory_region_init_rom(E1, NULL, E2, E3, E4);
+ memory_region_init_rom(E1, OBJECT(dev), E2, E3, E4);
|
- memory_region_init_ram(E1, NULL, E2, E3, E4);
+ memory_region_init_ram(E1, OBJECT(dev), E2, E3, E4);
|
- memory_region_init_ram_ptr(E1, NULL, E2, E3, E4);
+ memory_region_init_ram_ptr(E1, OBJECT(dev), E2, E3, E4);
|
- memory_region_init_ram_shared_nomigrate(E1, NULL, E2, E3, E4, E5);
+ memory_region_init_ram_shared_nomigrate(E1, OBJECT(dev), E2, E3, E4, E5);
)
  ...+>
}
