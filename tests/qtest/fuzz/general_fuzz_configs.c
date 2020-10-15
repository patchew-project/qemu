/*
 * General Virtual-Device Fuzzing Target Configs
 *
 * Copyright Red Hat Inc., 2020
 *
 * Authors:
 *  Alexander Bulekov   <alxndr@bu.edu>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"

#include "general_fuzz_configs.h"

/*
 * Specify pre-defined general-fuzz configs here.
 */
GArray *get_general_fuzz_configs(void){

    struct general_fuzz_config config;
    GArray *configs = g_array_new(false, false, sizeof(general_fuzz_config));

    config.name = "virtio-net-pci-slirp";
    config.args = "-M q35 -nodefaults "
        "-device virtio-net,netdev=net0 -netdev user,id=net0";
    config.objects = "virtio*";
    g_array_append_val(configs, config);

    config.name = "virtio-blk";
    config.args = "-machine q35 -device virtio-blk,drive=disk0 "
        "-drive file=null-co://,id=disk0,if=none,format=raw";
    config.objects = "virtio*";
    g_array_append_val(configs, config);

    config.name = "virtio-scsi";
    config.args = "-machine q35 -device virtio-scsi,num_queues=8 "
        "-device scsi-hd,drive=disk0 "
        "-drive file=null-co://,id=disk0,if=none,format=raw";
    config.objects = "scsi* virtio*";
    g_array_append_val(configs, config);

    config.name = "virtio-gpu";
    config.args = "-machine q35 -nodefaults -device virtio-gpu";
    config.objects = "virtio*";
    g_array_append_val(configs, config);

    config.name = "virtio-vga";
    config.args = "-machine q35 -nodefaults -device virtio-vga";
    config.objects = "virtio*";
    g_array_append_val(configs, config);

    config.name = "virtio-rng";
    config.args = "-machine q35 -nodefaults -device virtio-rng";
    config.objects = "virtio*";
    g_array_append_val(configs, config);

    config.name = "virtio-balloon";
    config.args = "-machine q35 -nodefaults -device virtio-balloon";
    config.objects = "virtio*";
    g_array_append_val(configs, config);

    config.name = "virtio-serial";
    config.args = "-machine q35 -nodefaults -device virtio-serial";
    config.objects = "virtio*";
    g_array_append_val(configs, config);

    config.name = "virtio-mouse";
    config.args = "-machine q35 -nodefaults -device virtio-mouse";
    config.objects = "virtio*";
    g_array_append_val(configs, config);

    config.name = "e1000";
    config.args = "-M q35 -nodefaults "
        "-device e1000,netdev=net0 -netdev user,id=net0";
    config.objects = "e1000";
    g_array_append_val(configs, config);

    config.name = "e1000e";
    config.args = "-M q35 -nodefaults "
        "-device e1000e,netdev=net0 -netdev user,id=net0";
    config.objects = "e1000e";
    g_array_append_val(configs, config);

    config.name = "cirrus-vga";
    config.args = "-machine q35 -nodefaults -device cirrus-vga";
    config.objects = "cirrus*";
    g_array_append_val(configs, config);

    config.name = "bochs-display";
    config.args = "-machine q35 -nodefaults -device bochs-display";
    config.objects = "bochs*";
    g_array_append_val(configs, config);

    config.name = "intel-hda";
    config.args = "-machine q35 -nodefaults -device intel-hda,id=hda0 "
        "-device hda-output,bus=hda0.0 -device hda-micro,bus=hda0.0 "
        "-device hda-duplex,bus=hda0.0";
    config.objects = "intel-hda";
    g_array_append_val(configs, config);

    config.name = "ide-hd";
    config.args = "-machine q35 -nodefaults "
        "-drive file=null-co://,if=none,format=raw,id=disk0 "
        "-device ide-hd,drive=disk0";
    config.objects = "ahci*";
    g_array_append_val(configs, config);

    config.name = "floppy";
    config.args = "-machine pc -nodefaults -device floppy,id=floppy0 "
        "-drive id=disk0,file=null-co://,file.read-zeroes=on,if=none "
        "-device floppy,drive=disk0,drive-type=288";
    config.objects = "fd* floppy*";
    g_array_append_val(configs, config);

    config.name = "xhci";
    config.args = "-machine q35 -nodefaults"
        "-drive file=null-co://,if=none,format=raw,id=disk0 "
        "-device qemu-xhci,id=xhci -device usb-tablet,bus=xhci.0 "
        "-device usb-bot -device usb-storage,drive=disk0 "
        "-chardev null,id=cd0 -chardev null,id=cd1 "
        "-device usb-braille,chardev=cd0 -device usb-ccid -device usb-ccid "
        "-device usb-kbd -device usb-mouse -device usb-serial,chardev=cd1 "
        "-device usb-tablet -device usb-wacom-tablet -device usb-audio";
    config.objects = "*usb* *uhci* *xhci*";
    g_array_append_val(configs, config);

    config.name = "pc-i440fx";
    config.args = "-machine pc";
    config.objects = "*";
    g_array_append_val(configs, config);

    config.name = "pc-q35";
    config.args = "-machine q35";
    config.objects = "*";
    g_array_append_val(configs, config);

    return configs;
}
