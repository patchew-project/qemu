HXCOMM Use DEFHEADING() to define headings in both help text and rST.
HXCOMM Text between SRST and ERST is copied to the rST version and
HXCOMM discarded from C version.
HXCOMM DEF(command, args, callback, arg_string, help) is used to construct
HXCOMM monitor info commands
HXCOMM HXCOMM can be used for comments, discarded from both rST and C.
HXCOMM
HXCOMM In this file, generally SRST fragments should have two extra
HXCOMM spaces of indent, so that the documentation list item for "virtio cmd"
HXCOMM appears inside the documentation list item for the top level
HXCOMM "virtio" documentation entry. The exception is the first SRST
HXCOMM fragment that defines that top level entry.

SRST
``virtio`` *subcommand*
  Show various information about virtio.

  Example:

  List all sub-commands::

    (qemu) virtio
    virtio query  -- List all available virtio devices
    virtio status path -- Display status of a given virtio device
    virtio queue-status path queue -- Display status of a given virtio queue
    virtio queue-element path queue [index] -- Display element of a given virtio queue

ERST

    {
        .name       = "query",
        .args_type  = "",
        .params     = "",
        .help       = "List all available virtio devices",
        .cmd        = hmp_virtio_query,
        .flags      = "p",
    },

SRST
  ``virtio query``
    List all available virtio devices

    Example:

    List all available virtio devices in the machine::

      (qemu) virtio query
      /machine/peripheral-anon/device[3]/virtio-backend [virtio-net]
      /machine/peripheral-anon/device[1]/virtio-backend [virtio-serial]
      /machine/peripheral-anon/device[0]/virtio-backend [virtio-blk]

ERST

    {
        .name       = "status",
        .args_type  = "path:s",
        .params     = "path",
        .help       = "Display status of a given virtio device",
        .cmd        = hmp_virtio_status,
        .flags      = "p",
    },

SRST
  ``virtio status`` *path*
    Display status of a given virtio device

    Example:

    Dump the status of the first virtio device::

      (qemu) virtio status /machine/peripheral-anon/device[3]/virtio-backend
      /machine/peripheral-anon/device[3]/virtio-backend:
        Device Id:        1
        Guest features:   event-idx, indirect-desc, version-1
                          ctrl-mac-addr, guest-announce, ctrl-vlan, ctrl-rx, ctrl-vq, status, mrg-rxbuf, host-ufo, host-ecn, host-tso6, host-tso4, guest-ufo, guest-ecn, guest-tso6, guest-tso4, mac, ctrl-guest-offloads, guest-csum, csum
        Host features:    event-idx, indirect-desc, bad-feature, version-1, any-layout, notify-on-empty
                          gso, ctrl-mac-addr, guest-announce, ctrl-rx-extra, ctrl-vlan, ctrl-rx, ctrl-vq, status, mrg-rxbuf, host-ufo, host-ecn, host-tso6, host-tso4, guest-ufo, guest-ecn, guest-tso6, guest-tso4, mac, ctrl-guest-offloads, guest-csum, csum
        Backend features:
        Endianness:       little
        VirtQueues:       3

ERST

    {
        .name       = "queue-status",
        .args_type  = "path:s,queue:i",
        .params     = "path queue",
        .help       = "Display status of a given virtio queue",
        .cmd        = hmp_virtio_queue_status,
        .flags      = "p",
    },

SRST
  ``virtio queue-status`` *path* *queue*
    Display status of a given virtio queue

    Example:

    Dump the status of the first queue of the first virtio device::

      (qemu) virtio queue-status /machine/peripheral-anon/device[3]/virtio-backend 0
      /machine/peripheral-anon/device[3]/virtio-backend:
        device_type:          virtio-net
        index:                0
        inuse:                0
        last_avail_idx:       61 (61 % 256)
        shadow_avail_idx:     292 (36 % 256)
        used_idx:             61 (61 % 256)
        signalled_used:       61 (61 % 256)
        signalled_used_valid: 1
        VRing:
          num:         256
          num_default: 256
          align:       4096
          desc:        0x000000006c352000
          avail:       0x000000006c353000
          used:        0x000000006c353240

ERST

    {
        .name       = "queue-element",
        .args_type  = "path:s,queue:i,index:i?",
        .params     = "path queue [index]",
        .help       = "Display element of a given virtio queue",
        .cmd        = hmp_virtio_queue_element,
        .flags      = "p",
    },

SRST
  ``virtio queue-element`` *path* *queue* [*index*]
    Display element of a given virtio queue

    Example:

    Dump the information of the head element of the first queue of
    the first virtio device::

      (qemu) virtio queue-element/machine/peripheral-anon/device[3]/virtio-backend 0
      index:  67
      ndescs: 1
      descs:  addr 0x6fe69800 len 1536 (write)

      (qemu) xp/128bx 0x6fe69800
      000000006fe69800: 0x02 0x00 0x00 0x00 0x00 0x00 0x00 0x00
      000000006fe69808: 0x00 0x00 0x01 0x00 0x52 0x54 0x00 0x12
      000000006fe69810: 0x34 0x56 0x52 0x54 0x00 0x09 0x51 0xde
      000000006fe69818: 0x08 0x00 0x45 0x00 0x00 0x4c 0x8f 0x32

    device[3] is a virtio-net device and we can see in the element buffer the
    MAC address of the card::

      [root@localhost ~]# ip link show ens4
      2: ens4: <BROADCAST,MULTICAST,UP,LOWER_UP> mtu 1500 qdisc fq_codel state UP m0
          link/ether 52:54:00:12:34:56 brd ff:ff:ff:ff:ff:ff

    and the MAC address of the gateway::

      [root@localhost ~]# arp -a
      _gateway (192.168.122.1) at 52:54:00:09:51:de [ether] on ens4

ERST
