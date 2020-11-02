===========================
eBPF qemu support
===========================

eBPF support (CONFIG_EBPF) is enabled automatically by 'configure' script
if 'bpf' system call is available.
To disable eBPF support use './configure --disable-bpf'

Basic eBPF functionality is located in ebpf/ebpf.c and ebpf/ebpf.h.
There are basic functions to load the eBPF program into the kernel.
Mostly, functions name are self-explanatory:

- `bpf_create_map()`, `bpf_lookup_element()`, `bpf_update_element()`, `bpf_delete_element()` - manages eBPF maps. On error, a basic error message would be reported and returned -1. On success, 0 would be returned(`bpf_create_map()` returns map's file descriptor).
- `bpf_prog_load()` - load the program. The program has to have proper map file descriptors if there are used. On error - the log eBPF would be reported. On success, the program file descriptor returned.
- `bpf_fixup_mapfd()` - would place map file descriptor into the program according to 'relocate array' of 'struct fixup_mapfd_t'. The function would return how many instructions were 'fixed' aka how many relocations was occurred.

Simplified workflow would look like this:

.. code:: C

    int map1 = bpf_create_map(...);
    int map2 = bpf_create_map(...);

    bpf_fixup_mapfd(<fixup table>, ARRAY_SIZE(<fixup table>), <instructions pointer>, ARRAY_SIZE(<instructions pointer>), <map1 name>, map1);
    bpf_fixup_mapfd(<fixup table>, ARRAY_SIZE(<fixup table>), <instructions pointer>, ARRAY_SIZE(<instructions pointer>), <map2 name>, map2);

    int prog = bpf_prog_load(<program type>, <instructions pointer>, ARRAY_SIZE(<instructions pointer>), "GPL");

See the bpf(2) for details.
