=============
zoned-storage
=============

Zoned Block Devices (ZBDs) devide the LBA space into block regions called zones
that are larger than the LBA size. It can only allow sequential writes, which
reduces write amplification in SSDs, leading to higher throughput and increased
capacity. More details about ZBDs can be found at:

https://zonedstorage.io/docs/introduction/zoned-storage

1. Block layer APIs for zoned storage
-------------------------------------
QEMU block layer has three zoned storage model:
- BLK_Z_HM: This model only allows sequential writes access. It supports a set
of ZBD-specific I/O request that used by the host to manage device zones.
- BLK_Z_HA: It deals with both sequential writes and random writes access.
- BLK_Z_NONE: Regular block devices and drive-managed ZBDs are treated as
non-zoned devices.

The block device information is resided inside BlockDriverState. QEMU uses
BlockLimits struct(BlockDriverState::bl) that is continuously accessed by the
block layer while processing I/O requests. A BlockBackend has a root pointer to
a BlockDriverState graph(for example, raw format on top of file-posix). The
zoned storage information can be propagated from the leaf BlockDriverState all
the way up to the BlockBackend. If the zoned storage model in file-posix is
set to BLK_Z_HM, then block drivers will declare support for zoned host device.

The block layer APIs support commands needed for zoned storage devices,
including report zones, four zone operations, and zone append.

2. Emulating zoned storage controllers
--------------------------------------
When the BlockBackend's BlockLimits model reports a zoned storage device, users
like the virtio-blk emulation or the qemu-io-cmds.c utility can use block layer
APIs for zoned storage emulation or testing.

For example, the command line for zone report testing a null_blk device of
qemu-io-cmds.c is:
$ path/to/qemu-io --image-opts driver=zoned_host_device,filename=/dev/nullb0 -c
"zrp offset nr_zones"
