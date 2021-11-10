HXCOMM Use DEFHEADING() to define headings in both help text and rST.
HXCOMM Text between SRST and ERST is copied to the rST version and
HXCOMM discarded from C version.
HXCOMM DEF(command, args, callback, arg_string, help) is used to construct
HXCOMM monitor info commands
HXCOMM HXCOMM can be used for comments, discarded from both rST and C.
HXCOMM
HXCOMM In this file, generally SRST fragments should have two extra
HXCOMM spaces of indent, so that the documentation list item for "info foo"
HXCOMM appears inside the documentation list item for the top level
HXCOMM "info" documentation entry. The exception is the first SRST
HXCOMM fragment that defines that top level entry.

SRST
``info`` *subcommand*
  Show various information about the system state.

ERST

    {
        .name       = "version",
        .args_type  = "",
        .params     = "",
        .help       = "show the version of QEMU",
        .cmd        = hmp_info_version,
        .flags      = "p",
    },

SRST
  ``info version``
    Show the version of QEMU.
ERST

    {
        .name       = "network",
        .args_type  = "",
        .params     = "",
        .help       = "show the network state",
        .cmd        = hmp_info_network,
    },

SRST
  ``info network``
    Show the network state.
ERST

    {
        .name       = "chardev",
        .args_type  = "",
        .params     = "",
        .help       = "show the character devices",
        .cmd        = hmp_info_chardev,
        .flags      = "p",
    },

SRST
  ``info chardev``
    Show the character devices.
ERST

    {
        .name       = "block",
        .args_type  = "nodes:-n,verbose:-v,device:B?",
        .params     = "[-n] [-v] [device]",
        .help       = "show info of one block device or all block devices "
                      "(-n: show named nodes; -v: show details)",
        .cmd        = hmp_info_block,
    },

SRST
  ``info block``
    Show info of one block device or all block devices.
ERST

    {
        .name       = "blockstats",
        .args_type  = "",
        .params     = "",
        .help       = "show block device statistics",
        .cmd        = hmp_info_blockstats,
    },

SRST
  ``info blockstats``
    Show block device statistics.
ERST

    {
        .name       = "block-jobs",
        .args_type  = "",
        .params     = "",
        .help       = "show progress of ongoing block device operations",
        .cmd        = hmp_info_block_jobs,
    },

SRST
  ``info block-jobs``
    Show progress of ongoing block device operations.
ERST

    {
        .name       = "registers",
        .args_type  = "cpustate_all:-a",
        .params     = "[-a]",
        .help       = "show the cpu registers (-a: all - show register info for all cpus)",
        .cmd        = hmp_info_registers,
    },

SRST
  ``info registers``
    Show the cpu registers.
ERST

#if defined(TARGET_I386)
    {
        .name       = "lapic",
        .args_type  = "apic-id:i?",
        .params     = "[apic-id]",
        .help       = "show local apic state (apic-id: local apic to read, default is which of current CPU)",

        .cmd        = hmp_info_local_apic,
    },
#endif

SRST
  ``info lapic``
    Show local APIC state
ERST

    {
        .name       = "cpus",
        .args_type  = "",
        .params     = "",
        .help       = "show infos for each CPU",
        .cmd        = hmp_info_cpus,
    },

SRST
  ``info cpus``
    Show infos for each CPU.
ERST

    {
        .name       = "history",
        .args_type  = "",
        .params     = "",
        .help       = "show the command line history",
        .cmd        = hmp_info_history,
        .flags      = "p",
    },

SRST
  ``info history``
    Show the command line history.
ERST

    {
        .name       = "irq",
        .args_type  = "",
        .params     = "",
        .help       = "show the interrupts statistics (if available)",
        .cmd_info_hrt = qmp_x_query_irq,
    },

SRST
  ``info irq``
    Show the interrupts statistics (if available).
ERST

    {
        .name       = "pic",
        .args_type  = "",
        .params     = "",
        .help       = "show PIC state",
        .cmd        = hmp_info_pic,
    },

SRST
  ``info pic``
    Show PIC state.
ERST

    {
        .name       = "rdma",
        .args_type  = "",
        .params     = "",
        .help       = "show RDMA state",
        .cmd_info_hrt = qmp_x_query_rdma,
    },

SRST
  ``info rdma``
    Show RDMA state.
ERST

    {
        .name       = "pci",
        .args_type  = "",
        .params     = "",
        .help       = "show PCI info",
        .cmd        = hmp_info_pci,
    },

SRST
  ``info pci``
    Show PCI information.
ERST

#if defined(TARGET_I386) || defined(TARGET_SH4) || defined(TARGET_SPARC) || \
    defined(TARGET_PPC) || defined(TARGET_XTENSA) || defined(TARGET_M68K)
    {
        .name       = "tlb",
        .args_type  = "",
        .params     = "",
        .help       = "show virtual to physical memory mappings",
        .cmd        = hmp_info_tlb,
    },
#endif

SRST
  ``info tlb``
    Show virtual to physical memory mappings.
ERST

#if defined(TARGET_I386) || defined(TARGET_RISCV)
    {
        .name       = "mem",
        .args_type  = "",
        .params     = "",
        .help       = "show the active virtual memory mappings",
        .cmd        = hmp_info_mem,
    },
#endif

SRST
  ``info mem``
    Show the active virtual memory mappings.
ERST

    {
        .name       = "mtree",
        .args_type  = "flatview:-f,dispatch_tree:-d,owner:-o,disabled:-D",
        .params     = "[-f][-d][-o][-D]",
        .help       = "show memory tree (-f: dump flat view for address spaces;"
                      "-d: dump dispatch tree, valid with -f only);"
                      "-o: dump region owners/parents;"
                      "-D: dump disabled regions",
        .cmd        = hmp_info_mtree,
    },

SRST
  ``info mtree``
    Show memory tree.
ERST

#if defined(CONFIG_TCG)
    {
        .name       = "jit",
        .args_type  = "",
        .params     = "",
        .help       = "show dynamic compiler info",
    },
#endif

SRST
  ``info jit``
    Show dynamic compiler info.
ERST

#if defined(CONFIG_TCG)
    {
        .name       = "opcount",
        .args_type  = "",
        .params     = "",
        .help       = "show dynamic compiler opcode counters",
    },
#endif

SRST
  ``info opcount``
    Show dynamic compiler opcode counters
ERST

    {
        .name       = "sync-profile",
        .args_type  = "mean:-m,no_coalesce:-n,max:i?",
        .params     = "[-m] [-n] [max]",
        .help       = "show synchronization profiling info, up to max entries "
                      "(default: 10), sorted by total wait time. (-m: sort by "
                      "mean wait time; -n: do not coalesce objects with the "
                      "same call site)",
        .cmd        = hmp_info_sync_profile,
    },

SRST
  ``info sync-profile [-m|-n]`` [*max*]
    Show synchronization profiling info, up to *max* entries (default: 10),
    sorted by total wait time.

    ``-m``
      sort by mean wait time
    ``-n``
      do not coalesce objects with the same call site

    When different objects that share the same call site are coalesced,
    the "Object" field shows---enclosed in brackets---the number of objects
    being coalesced.
ERST

    {
        .name       = "kvm",
        .args_type  = "",
        .params     = "",
        .help       = "show KVM information",
        .cmd        = hmp_info_kvm,
    },

SRST
  ``info kvm``
    Show KVM information.
ERST

    {
        .name       = "numa",
        .args_type  = "",
        .params     = "",
        .help       = "show NUMA information",
        .cmd_info_hrt = qmp_x_query_numa,
    },

SRST
  ``info numa``
    Show NUMA information.
ERST

    {
        .name       = "usb",
        .args_type  = "",
        .params     = "",
        .help       = "show guest USB devices",
        .cmd_info_hrt = qmp_x_query_usb,
    },

SRST
  ``info usb``
    Show guest USB devices.
ERST

    {
        .name       = "usbhost",
        .args_type  = "",
        .params     = "",
        .help       = "show host USB devices",
    },

SRST
  ``info usbhost``
    Show host USB devices.
ERST

    {
        .name       = "profile",
        .args_type  = "",
        .params     = "",
        .help       = "show profiling information",
        .cmd_info_hrt = qmp_x_query_profile,
    },

SRST
  ``info profile``
    Show profiling information.
ERST

    {
        .name       = "capture",
        .args_type  = "",
        .params     = "",
        .help       = "show capture information",
        .cmd        = hmp_info_capture,
    },

SRST
  ``info capture``
    Show capture information.
ERST

    {
        .name       = "snapshots",
        .args_type  = "",
        .params     = "",
        .help       = "show the currently saved VM snapshots",
        .cmd        = hmp_info_snapshots,
    },

SRST
  ``info snapshots``
    Show the currently saved VM snapshots.
ERST

    {
        .name       = "status",
        .args_type  = "",
        .params     = "",
        .help       = "show the current VM status (running|paused)",
        .cmd        = hmp_info_status,
        .flags      = "p",
    },

SRST
  ``info status``
    Show the current VM status (running|paused).
ERST

    {
        .name       = "mice",
        .args_type  = "",
        .params     = "",
        .help       = "show which guest mouse is receiving events",
        .cmd        = hmp_info_mice,
    },

SRST
  ``info mice``
    Show which guest mouse is receiving events.
ERST

#if defined(CONFIG_VNC)
    {
        .name       = "vnc",
        .args_type  = "",
        .params     = "",
        .help       = "show the vnc server status",
        .cmd        = hmp_info_vnc,
    },
#endif

SRST
  ``info vnc``
    Show the vnc server status.
ERST

#if defined(CONFIG_SPICE)
    {
        .name       = "spice",
        .args_type  = "",
        .params     = "",
        .help       = "show the spice server status",
        .cmd        = hmp_info_spice,
    },
#endif

SRST
  ``info spice``
    Show the spice server status.
ERST

    {
        .name       = "name",
        .args_type  = "",
        .params     = "",
        .help       = "show the current VM name",
        .cmd        = hmp_info_name,
        .flags      = "p",
    },

SRST
  ``info name``
    Show the current VM name.
ERST

    {
        .name       = "uuid",
        .args_type  = "",
        .params     = "",
        .help       = "show the current VM UUID",
        .cmd        = hmp_info_uuid,
        .flags      = "p",
    },

SRST
  ``info uuid``
    Show the current VM UUID.
ERST

#if defined(CONFIG_SLIRP)
    {
        .name       = "usernet",
        .args_type  = "",
        .params     = "",
        .help       = "show user network stack connection states",
        .cmd        = hmp_info_usernet,
    },
#endif

SRST
  ``info usernet``
    Show user network stack connection states.
ERST

    {
        .name       = "migrate",
        .args_type  = "",
        .params     = "",
        .help       = "show migration status",
        .cmd        = hmp_info_migrate,
    },

SRST
  ``info migrate``
    Show migration status.
ERST

    {
        .name       = "migrate_capabilities",
        .args_type  = "",
        .params     = "",
        .help       = "show current migration capabilities",
        .cmd        = hmp_info_migrate_capabilities,
    },

SRST
  ``info migrate_capabilities``
    Show current migration capabilities.
ERST

    {
        .name       = "migrate_parameters",
        .args_type  = "",
        .params     = "",
        .help       = "show current migration parameters",
        .cmd        = hmp_info_migrate_parameters,
    },

SRST
  ``info migrate_parameters``
    Show current migration parameters.
ERST

    {
        .name       = "balloon",
        .args_type  = "",
        .params     = "",
        .help       = "show balloon information",
        .cmd        = hmp_info_balloon,
    },

SRST
  ``info balloon``
    Show balloon information.
ERST

    {
        .name       = "qtree",
        .args_type  = "",
        .params     = "",
        .help       = "show device tree",
        .cmd        = hmp_info_qtree,
    },

SRST
  ``info qtree``
    Show device tree.
ERST

    {
        .name       = "qdm",
        .args_type  = "",
        .params     = "",
        .help       = "show qdev device model list",
        .cmd        = hmp_info_qdm,
    },

SRST
  ``info qdm``
    Show qdev device model list.
ERST

    {
        .name       = "qom-tree",
        .args_type  = "path:s?",
        .params     = "[path]",
        .help       = "show QOM composition tree",
        .cmd        = hmp_info_qom_tree,
        .flags      = "p",
    },

SRST
  ``info qom-tree``
    Show QOM composition tree.
ERST

    {
        .name       = "roms",
        .args_type  = "",
        .params     = "",
        .help       = "show roms",
        .cmd_info_hrt = qmp_x_query_roms,
    },

SRST
  ``info roms``
    Show roms.
ERST

    {
        .name       = "trace-events",
        .args_type  = "name:s?,vcpu:i?",
        .params     = "[name] [vcpu]",
        .help       = "show available trace-events & their state "
                      "(name: event name pattern; vcpu: vCPU to query, default is any)",
        .cmd = hmp_info_trace_events,
        .command_completion = info_trace_events_completion,
    },

SRST
  ``info trace-events``
    Show available trace-events & their state.
ERST

    {
        .name       = "tpm",
        .args_type  = "",
        .params     = "",
        .help       = "show the TPM device",
        .cmd        = hmp_info_tpm,
    },

SRST
  ``info tpm``
    Show the TPM device.
ERST

    {
        .name       = "memdev",
        .args_type  = "",
        .params     = "",
        .help       = "show memory backends",
        .cmd        = hmp_info_memdev,
        .flags      = "p",
    },

SRST
  ``info memdev``
    Show memory backends
ERST

    {
        .name       = "memory-devices",
        .args_type  = "",
        .params     = "",
        .help       = "show memory devices",
        .cmd        = hmp_info_memory_devices,
    },

SRST
  ``info memory-devices``
    Show memory devices.
ERST

    {
        .name       = "iothreads",
        .args_type  = "",
        .params     = "",
        .help       = "show iothreads",
        .cmd        = hmp_info_iothreads,
        .flags      = "p",
    },

SRST
  ``info iothreads``
    Show iothread's identifiers.
ERST

    {
        .name       = "rocker",
        .args_type  = "name:s",
        .params     = "name",
        .help       = "Show rocker switch",
        .cmd        = hmp_rocker,
    },

SRST
  ``info rocker`` *name*
    Show rocker switch.
ERST

    {
        .name       = "rocker-ports",
        .args_type  = "name:s",
        .params     = "name",
        .help       = "Show rocker ports",
        .cmd        = hmp_rocker_ports,
    },

SRST
  ``info rocker-ports`` *name*-ports
    Show rocker ports.
ERST

    {
        .name       = "rocker-of-dpa-flows",
        .args_type  = "name:s,tbl_id:i?",
        .params     = "name [tbl_id]",
        .help       = "Show rocker OF-DPA flow tables",
        .cmd        = hmp_rocker_of_dpa_flows,
    },

SRST
  ``info rocker-of-dpa-flows`` *name* [*tbl_id*]
    Show rocker OF-DPA flow tables.
ERST

    {
        .name       = "rocker-of-dpa-groups",
        .args_type  = "name:s,type:i?",
        .params     = "name [type]",
        .help       = "Show rocker OF-DPA groups",
        .cmd        = hmp_rocker_of_dpa_groups,
    },

SRST
  ``info rocker-of-dpa-groups`` *name* [*type*]
    Show rocker OF-DPA groups.
ERST

#if defined(TARGET_S390X)
    {
        .name       = "skeys",
        .args_type  = "addr:l",
        .params     = "address",
        .help       = "Display the value of a storage key",
        .cmd        = hmp_info_skeys,
    },
#endif

SRST
  ``info skeys`` *address*
    Display the value of a storage key (s390 only)
ERST

#if defined(TARGET_S390X)
    {
        .name       = "cmma",
        .args_type  = "addr:l,count:l?",
        .params     = "address [count]",
        .help       = "Display the values of the CMMA storage attributes for a range of pages",
        .cmd        = hmp_info_cmma,
    },
#endif

SRST
  ``info cmma`` *address*
    Display the values of the CMMA storage attributes for a range of
    pages (s390 only)
ERST

    {
        .name       = "dump",
        .args_type  = "",
        .params     = "",
        .help       = "Display the latest dump status",
        .cmd        = hmp_info_dump,
    },

SRST
  ``info dump``
    Display the latest dump status.
ERST

    {
        .name       = "ramblock",
        .args_type  = "",
        .params     = "",
        .help       = "Display system ramblock information",
        .cmd_info_hrt = qmp_x_query_ramblock,
    },

SRST
  ``info ramblock``
    Dump all the ramblocks of the system.
ERST

    {
        .name       = "hotpluggable-cpus",
        .args_type  = "",
        .params     = "",
        .help       = "Show information about hotpluggable CPUs",
        .cmd        = hmp_hotpluggable_cpus,
        .flags      = "p",
    },

SRST
  ``info hotpluggable-cpus``
    Show information about hotpluggable CPUs
ERST

    {
        .name       = "vm-generation-id",
        .args_type  = "",
        .params     = "",
        .help       = "Show Virtual Machine Generation ID",
        .cmd = hmp_info_vm_generation_id,
    },

SRST
  ``info vm-generation-id``
    Show Virtual Machine Generation ID
ERST

    {
        .name       = "memory_size_summary",
        .args_type  = "",
        .params     = "",
        .help       = "show the amount of initially allocated and "
                      "present hotpluggable (if enabled) memory in bytes.",
        .cmd        = hmp_info_memory_size_summary,
    },

SRST
  ``info memory_size_summary``
    Display the amount of initially allocated and present hotpluggable (if
    enabled) memory in bytes.
ERST

#if defined(TARGET_I386)
    {
        .name       = "sev",
        .args_type  = "",
        .params     = "",
        .help       = "show SEV information",
        .cmd        = hmp_info_sev,
    },
#endif

SRST
  ``info sev``
    Show SEV information.
ERST

    {
        .name       = "replay",
        .args_type  = "",
        .params     = "",
        .help       = "show record/replay information",
        .cmd        = hmp_info_replay,
    },

SRST
  ``info replay``
    Display the record/replay information: mode and the current icount.
ERST

    {
        .name       = "dirty_rate",
        .args_type  = "",
        .params     = "",
        .help       = "show dirty rate information",
        .cmd        = hmp_info_dirty_rate,
    },

SRST
  ``info dirty_rate``
    Display the vcpu dirty rate information.
ERST

#if defined(TARGET_I386)
    {
        .name       = "sgx",
        .args_type  = "",
        .params     = "",
        .help       = "show intel SGX information",
        .cmd        = hmp_info_sgx,
    },
#endif

SRST
  ``info sgx``
    Show intel SGX information.
ERST

    {
        .name      = "virtio",
        .args_type = "",
        .params    = "",
        .help      = "List all available virtio devices",
        .cmd       = hmp_virtio_query,
        .flags     = "p",
    },

SRST
  ``info virtio``
    List all available virtio devices

    Example:

    List all available virtio devices in the machine::

    (qemu) info virtio
    /machine/peripheral/vsock0/virtio-backend [vhost-vsock]
    /machine/peripheral/crypto0/virtio-backend [virtio-crypto]
    /machine/peripheral-anon/device[2]/virtio-backend [virtio-scsi]
    /machine/peripheral-anon/device[1]/virtio-backend [virtio-net]
    /machine/peripheral-anon/device[0]/virtio-backend [virtio-serial]

ERST

    {
        .name      = "virtio-status",
        .args_type = "path:s",
        .params    = "path",
        .help      = "Display status of a given virtio device",
        .cmd       = hmp_virtio_status,
        .flags     = "p",
    },

SRST
  ``info virtio-status`` *path*
    Display status of a given virtio device

    Example:

    Dump the status of virtio-net (vhost on)::

    (qemu) info virtio-status /machine/peripheral-anon/device[1]/virtio-backend
    /machine/peripheral-anon/device[1]/virtio-backend:
        device_name:             virtio-net (vhost)
        device_id:               1
        vhost_started:           true
        bus_name:                (null)
        broken:                  false
        disabled:                false
        disable_legacy_check:    false
        started:                 true
        use_started:             true
        start_on_kick:           false
        use_guest_notifier_mask: true
        vm_running:              true
        num_vqs:                 3
        queue_sel:               2
        isr:                     1
        endianness:              little
        status: acknowledge, driver, features-ok, driver-ok
        Guest features:   event-idx, indirect-desc, version-1
                          ctrl-mac-addr, guest-announce, ctrl-vlan, ctrl-rx, ctrl-vq, status, mrg-rxbuf,
                          host-ufo, host-ecn, host-tso6, host-tso4, guest-ufo, guest-ecn, guest-tso6,
                          guest-tso4, mac, ctrl-guest-offloads, guest-csum, csum
        Host features:    protocol-features, event-idx, indirect-desc, version-1, any-layout,
                          notify-on-empty, gso, ctrl-mac-addr, guest-announce, ctrl-rx-extra, ctrl-vlan,
                          ctrl-rx, ctrl-vq, status, mrg-rxbuf, host-ufo, host-ecn, host-tso6, host-tso4,
                          guest-ufo, guest-ecn, guest-tso6, guest-tso4, mac, ctrl-guest-offloads,
                          guest-csum, csum
        Backend features: protocol-features, event-idx, indirect-desc, version-1, any-layout,
                          notify-on-empty, gso, ctrl-mac-addr, guest-announce, ctrl-rx-extra, ctrl-vlan,
                          ctrl-rx, ctrl-vq, status, mrg-rxbuf, host-ufo, host-ecn, host-tso6, host-tso4,
                          guest-ufo, guest-ecn, guest-tso6, guest-tso4, mac, ctrl-guest-offloads,
                          guest-csum, csum
        VHost:
          nvqs:           2
          vq_index:       0
          max_queues:     1
          n_mem_sections: 4
          n_tmp_sections: 4
          backend_cap:    2
          log_enabled:    false
          log_size:       0
          Features:       event-idx, indirect-desc, iommu-platform, version-1, any-layout,
                          notify-on-empty, log-all, mrg-rxbuf
        Acked features:   event-idx, indirect-desc, version-1, mrg-rxbuf
        Backend features:
        Protocol features:

ERST

    {
        .name      = "virtio-queue-status",
        .args_type = "path:s,queue:i",
        .params    = "path queue",
        .help      = "Display status of a given virtio queue",
        .cmd       = hmp_virtio_queue_status,
        .flags     = "p",
    },

SRST
  ``info virtio-queue-status`` *path* *queue*
    Display status of a given virtio queue

    Example:

    Dump the status of the 6th queue of virtio-scsi::

    (qemu) info virtio-queue-status /machine/peripheral-anon/device[2]/virtio-backend 5
    /machine/peripheral-anon/device[2]/virtio-backend:
        device_name:          virtio-scsi
        queue_index:          5
        inuse:                0
        used_idx:             605
        signalled_used:       605
        signalled_used_valid: true
        last_avail_idx:       605
        shadow_avail_idx:     605
        VRing:
          num:          256
          num_default:  256
          align:        4096
          desc:         0x000000011f0bc000
          avail:        0x000000011f0bd000
          used:         0x000000011f0bd240

ERST

    {
        .name      = "virtio-vhost-queue-status",
        .args_type = "path:s,queue:i",
        .params    = "path queue",
        .help      = "Display status of a given vhost queue",
        .cmd       = hmp_vhost_queue_status,
        .flags     = "p",
    },

SRST
  ``info virtio-vhost-queue-status`` *path* *queue*
    Display status of a given vhost queue

    Example:

    (qemu) info virtio-vhost-queue-status /machine/peripheral/vsock0/virtio-backend 1
    /machine/peripheral/vsock0/virtio-backend:
        device_name:          vhost-vsock (vhost)
        kick:                 0
        call:                 0
        VRing:
          num:         128
          desc:        0x00007f44fe5b2000
          desc_phys:   0x000000011f3fb000
          desc_size:   2048
          avail:       0x00007f44fe5b2800
          avail_phys:  0x000000011f3fb800
          avail_size:  262
          used:        0x00007f44fe5b2940
          used_phys:   0x000000011f3fb940
          used_size:   1030

ERST

    {
        .name       = "virtio-queue-element",
        .args_type  = "path:s,queue:i,index:i?",
        .params     = "path queue [index]",
        .help       = "Display element of a given virtio queue",
        .cmd        = hmp_virtio_queue_element,
        .flags      = "p",
    },

SRST
  ``info virtio-queue-element`` *path* *queue* [*index*]
    Display element of a given virtio queue

    Example:

    Dump the information of the head element of the first queue of
    virtio-net (vhost on)::

    (qemu) info virtio-queue-element /machine/peripheral-anon/device[1]/virtio-backend 0
    /machine/peripheral-anon/device[1]/virtio-backend:
        device_name: virtio-net
        index:       0
        desc:
          ndescs:  1
          descs:   addr 0x1312c8000 len 1536 (write)
        avail:
          flags: 0
          idx:   256
          ring:  0
        used:
          flags: 0
          idx:   32

    Since device[1] is a virtio-net device, we can see the MAC address
    of the NIC in the element buffer::

    (qemu) xp/128bx 0x1312c8000
    00000001312c8000: 0x01 0x00 0x00 0x00 0x00 0x00 0x22 0x00
    00000001312c8008: 0x06 0x00 0x01 0x00 0x52 0x54 0x00 0x12
    00000001312c8010: 0x34 0x56 0xe6 0x94 0xf2 0xc1 0x51 0x2a
    ...

    [root@guest: ~]# ip link show eth0
    2: eth0: <BROADCAST,MULTICAST,UP,LOWER_UP> mtu 1500 qdisc pfifo_fast state UP mode
    DEFAULT group default qlen 1000
        link/ether 52:54:00:12:34:56 brd ff:ff:ff:ff:ff:ff

    And we can see the MAC address of the gateway immediately after::

    [root@guest: ~]# arp -a
    gateway (192.168.53.1) at e6:94:f2:c1:51:2a [ether] on eth0

ERST
