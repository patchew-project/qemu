HXCOMM Use DEFHEADING() to define headings in both help text and rST.
HXCOMM Text between SRST and ERST is copied to the rST version and
HXCOMM discarded from C version.
HXCOMM
HXCOMM DEF(command, args, callback, arg_string, help) is used to construct
HXCOMM monitor info commands.
HXCOMM
HXCOMM HXCOMM can be used for comments, discarded from both rST and C.
HXCOMM
HXCOMM In this file, generally SRST fragments should have two extra
HXCOMM spaces of indent, so that the documentation list item for "virtio cmd"
HXCOMM appears inside the documentation list item for the top level
HXCOMM "virtio" documentation entry. The exception is the first SRST
HXCOMM fragment that defines that top level entry.

SRST
  ``virtio`` *subcommand*
  Show various information about virtio

  Example:

  List all sub-commands::

  (qemu) virtio
  virtio query  -- List all available virtio devices
  virtio status path -- Display status of a given virtio device
  virtio queue-status path queue -- Display status of a given virtio queue
  virtio vhost-queue-status path queue -- Display status of a given vhost queue
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
  /machine/peripheral/vsock0/virtio-backend [vhost-vsock]
  /machine/peripheral/crypto0/virtio-backend [virtio-crypto]
  /machine/peripheral-anon/device[2]/virtio-backend [virtio-scsi]
  /machine/peripheral-anon/device[1]/virtio-backend [virtio-net]
  /machine/peripheral-anon/device[0]/virtio-backend [virtio-serial]

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

  Dump the status of virtio-net (vhost on)::

  (qemu) virtio status /machine/peripheral-anon/device[1]/virtio-backend
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
    Host features:    protocol-features, event-idx, indirect-desc, version-1, any-layout, notify-on-empty
                      gso, ctrl-mac-addr, guest-announce, ctrl-rx-extra, ctrl-vlan, ctrl-rx, ctrl-vq,
                      status, mrg-rxbuf, host-ufo, host-ecn, host-tso6, host-tso4, guest-ufo, guest-ecn,
                      guest-tso6, guest-tso4, mac, ctrl-guest-offloads, guest-csum, csum
    Backend features: protocol-features, event-idx, indirect-desc, version-1, any-layout, notify-on-empty
                      gso, ctrl-mac-addr, guest-announce, ctrl-rx-extra, ctrl-vlan, ctrl-rx, ctrl-vq,
                      status, mrg-rxbuf, host-ufo, host-ecn, host-tso6, host-tso4, guest-ufo, guest-ecn,
                      guest-tso6, guest-tso4, mac, ctrl-guest-offloads, guest-csum, csum
    VHost:
      nvqs:           2
      vq_index:       0
      max_queues:     1
      n_mem_sections: 4
      n_tmp_sections: 4
      backend_cap:    2
      log_enabled:    false
      log_size:       0
      Features:          event-idx, indirect-desc, iommu-platform, version-1, any-layout, notify-on-empty
                         log-all, mrg-rxbuf
      Acked features:    event-idx, indirect-desc, version-1
                         mrg-rxbuf
      Backend features:
      Protocol features:

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

  Dump the status of the 6th queue of virtio-scsi::

  (qemu) virtio queue-status /machine/peripheral-anon/device[2]/virtio-backend 5
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
    .name       = "vhost-queue-status",
    .args_type  = "path:s,queue:i",
    .params     = "path queue",
    .help       = "Display status of a given vhost queue",
    .cmd        = hmp_vhost_queue_status,
    .flags      = "p",
  },

SRST
  ``virtio vhost-queue-status`` *path* *queue*
  Display status of a given vhost queue

  Example:

  Dump the status of the 2nd queue of vhost-vsock::

  (qemu) virtio vhost-queue-status /machine/peripheral/vsock0/virtio-backend 1
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
  virtio-net (vhost on)::

  (qemu) virtio queue-element /machine/peripheral-anon/device[1]/virtio-backend 0
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
