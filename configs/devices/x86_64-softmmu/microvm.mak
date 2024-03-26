# SPDX-FileCopyrightText: 2024 Linaro Ltd.
#
# Config that only supports the 64-bit microvm machine.
# This avoids bringing in any of numerous legacy features from
# the legacy machines or the 32bit platform.
#

CONFIG_MICROVM=y
CONFIG_PCI_DEVICES=n
CONFIG_SMBIOS=y
CONFIG_SMBIOS_LEGACY=n
CONFIG_VIRTIO_BALLOON=y
CONFIG_VIRTIO_BLK=y
CONFIG_VIRTIO_CRYPTO=y
CONFIG_VIRTIO_GPU=y
CONFIG_VIRTIO_INPUT=y
CONFIG_VIRTIO_NET=y
CONFIG_VIRTIO_RNG=y
CONFIG_VIRTIO_SCSI=y
CONFIG_VIRTIO_SERIAL=y
