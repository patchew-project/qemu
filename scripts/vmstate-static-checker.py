#!/usr/bin/python
#
# Compares vmstate information stored in JSON format, obtained from
# the -dump-vmstate QEMU command.
#
# Copyright 2014 Amit Shah <amit.shah@redhat.com>
# Copyright 2014 Red Hat, Inc.
#
# This program is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation; either version 2 of the License, or
# (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License along
# with this program; if not, see <http://www.gnu.org/licenses/>.

#
# 2017 Deepak Verma <dverma@redhat.com>
# Added few functions and fields for whitelisting
#

import argparse
import json
import sys

# Count the number of errors found
taint = 0


def bump_taint():
    global taint

    # Ensure we don't wrap around or reset to 0 -- the shell only has
    # an 8-bit return value.
    if taint < 255:
        taint = taint + 1

# Sections gain/lose new fields with time.
# These are not name changes thats handled by another list.
# These will be 'missing' or 'not found' in different versions of qemu


def check_updated_properties(src_desc, field):
    src_desc = str(src_desc)
    field = str(field)
    updated_property = {
      'ICH9LPC': ['ICH9LPC/smi_feat'],
      'ide_bus/error': ['retry_sector_num', 'retry_nsector', 'retry_unit'],
      'e1000': ['e1000/full_mac_state'],
      'ich9_pm': ['ich9_pm/tco', 'ich9_pm/cpuhp']
    }

    if src_desc in updated_property and field in updated_property[src_desc]:
        return True

    return False


# A lot of errors are generated due to differences in sizes some of which are
# false positives. This list is used to save those common changes
def check_updated_sizes(field, old_size, new_size):
    new_sizes_list = {
            'tally_counters.TxOk': [8, 64],
            'intel-iommu': [0, 1],
            'iommu-intel': [0, 1]
            }

    if field not in new_sizes_list:
        return False

    if(old_size in new_sizes_list[field] and new_size in
            new_sizes_list[field]):
        return True

    return False


# With time new sections/hardwares supported and old ones are depreciated on
# chipsets.
# There is no separate list for new or dead sections as it's relative to which
# qemu version you compare too.
# Update this list with such sections.
# some items in this list might overlap with changed sections names.
def check_new_sections(sec):
    new_sections_list = [
            'virtio-balloon-device',
            'virtio-rng-device',
            'virtio-scsi-device',
            'virtio-blk-device',
            'virtio-serial-device',
            'virtio-net-device',
            'vhost-vsock-device',
            'virtio-input-host-device',
            'virtio-input-hid-device',
            'virtio-mouse-device',
            'virtio-keyboard-device',
            'virtio-vga',
            'virtio-input-device',
            'virtio-gpu-device',
            'virtio-tablet-device',
            'isa-pcspk',
            'qemu-xhci',
            'base-xhci',
            'vmgenid',
            'intel-iommu',
            'i8257',
            'i82801b11-bridge',
            'ivshmem',
            'ivshmem-doorbell',
            'ivshmem-plain',
            'usb-storage-device',
            'usb-storage-dev',
            'pci-qxl',
            'pci-uhci-usb',
            'pci-piix3',
            'pci-vga',
            'pci-bridge-seat',
            'pcie-root-port',
            'fw_cfg_io',
            'fw_cfg_mem',
            'exynos4210-ehci-usb',
            'sysbus-ehci-usb',
            'tegra2-ehci-usb',
            'kvm-apic',
            'fusbh200-ehci-usb',
            'apic',
            'apic-common',
            'xlnx,ps7-usb',
            'e1000e',
            'e1000-82544gc',
            'e1000-82545em']

    if sec in new_sections_list:
        return True

    return False

# Fields might change name with time across qemu versions.


def check_fields_match(name, s_field, d_field):
    if s_field == d_field:
        return True

    # Some fields changed names between qemu versions.  This list
    # is used to whitelist such changes in each section / description.
    changed_names = {
        'apic': ['timer', 'timer_expiry'],
        'ehci': ['dev', 'pcidev'],
        'I440FX': ['dev', 'parent_obj'],
        'ich9_ahci': ['card', 'parent_obj'],
        'ich9-ahci': ['ahci', 'ich9_ahci'],
        'ioh3420': ['PCIDevice', 'PCIEDevice'],
        'ioh-3240-express-root-port': ['port.br.dev',
                                       'parent_obj.parent_obj.parent_obj',
                                       'port.br.dev.exp.aer_log',
                            'parent_obj.parent_obj.parent_obj.exp.aer_log'],
        'cirrus_vga': ['hw_cursor_x', 'vga.hw_cursor_x',
                       'hw_cursor_y', 'vga.hw_cursor_y'],
        'lsiscsi': ['dev', 'parent_obj'],
        'mch': ['d', 'parent_obj'],
        'pci_bridge': ['bridge.dev', 'parent_obj', 'bridge.dev.shpc', 'shpc'],
        'pcnet': ['pci_dev', 'parent_obj'],
        'PIIX3': ['pci_irq_levels', 'pci_irq_levels_vmstate'],
        'piix4_pm': ['dev', 'parent_obj', 'pci0_status',
                     'acpi_pci_hotplug.acpi_pcihp_pci_status[0x0]',
                     'pm1a.sts', 'ar.pm1.evt.sts', 'pm1a.en', 'ar.pm1.evt.en',
                     'pm1_cnt.cnt', 'ar.pm1.cnt.cnt',
                     'tmr.timer', 'ar.tmr.timer',
                     'tmr.overflow_time', 'ar.tmr.overflow_time',
                     'gpe', 'ar.gpe'],
        'qxl': ['num_surfaces', 'ssd.num_surfaces'],
        'usb-ccid': ['abProtocolDataStructure',
                     'abProtocolDataStructure.data'],
        'usb-host': ['dev', 'parent_obj'],
        'usb-mouse': ['usb-ptr-queue', 'HIDPointerEventQueue'],
        'usb-tablet': ['usb-ptr-queue', 'HIDPointerEventQueue'],
        'vmware_vga': ['card', 'parent_obj'],
        'vmware_vga_internal': ['depth', 'new_depth'],
        'xhci': ['pci_dev', 'parent_obj'],
        'x3130-upstream': ['PCIDevice', 'PCIEDevice'],
        'xio3130-express-downstream-port': ['port.br.dev',
                                            'parent_obj.parent_obj.parent_obj',
                                            'port.br.dev.exp.aer_log',
                            'parent_obj.parent_obj.parent_obj.exp.aer_log'],
        'xio3130-downstream': ['PCIDevice', 'PCIEDevice'],
        'xio3130-express-upstream-port': ['br.dev', 'parent_obj.parent_obj',
                                          'br.dev.exp.aer_log',
                                          'parent_obj.parent_obj.exp.aer_log'],
        'spapr_pci': ['dma_liobn[0]', 'mig_liobn',
                      'mem_win_addr', 'mig_mem_win_addr',
                      'mem_win_size', 'mig_mem_win_size',
                      'io_win_addr', 'mig_io_win_addr',
                      'io_win_size', 'mig_io_win_size'],
        'rtl8139': ['dev', 'parent_obj'],
        'e1000e': ['PCIDevice', 'PCIEDevice', 'intr_state',
                   'redhat_7_3_intr_state'],
        'nec-usb-xhci': ['PCIDevice', 'PCIEDevice'],
        'xhci-intr': ['er_full_unused', 'er_full'],
        'e1000': ['dev', 'parent_obj',
                  'tx.ipcss', 'tx.props.ipcss',
                  'tx.ipcso', 'tx.props.ipcso',
                  'tx.ipcse', 'tx.props.ipcse',
                  'tx.tucss', 'tx.props.tucss',
                  'tx.tucso', 'tx.props.tucso',
                  'tx.tucse', 'tx.props.tucse',
                  'tx.paylen', 'tx.props.paylen',
                  'tx.hdr_len', 'tx.props.hdr_len',
                  'tx.mss', 'tx.props.mss',
                  'tx.sum_needed', 'tx.props.sum_needed',
                  'tx.ip', 'tx.props.ip',
                  'tx.tcp', 'tx.props.tcp',
                  'tx.ipcss', 'tx.props.ipcss',
                  'tx.ipcss', 'tx.props.ipcss',
                  ]
    }

    if name not in changed_names:
        return False

    if s_field in changed_names[name] and d_field in changed_names[name]:
        return True

    return False


def get_changed_sec_name(sec):
    # Section names can change -- see commit 292b1634 for an example.
    changes = {
        "ICH9 LPC": "ICH9-LPC",
        "e1000-82540em": "e1000",
    }

    for item in changes:
        if item == sec:
            return changes[item]
        if changes[item] == sec:
            return item
    return ""


def exists_in_substruct(fields, item):
    # Some QEMU versions moved a few fields inside a substruct.  This
    # kept the on-wire format the same.  This function checks if
    # something got shifted inside a substruct.  For example, the
    # change in commit 1f42d22233b4f3d1a2933ff30e8d6a6d9ee2d08f

    if "Description" not in fields:
        return False

    if "Fields" not in fields["Description"]:
        return False

    substruct_fields = fields["Description"]["Fields"]

    if substruct_fields == []:
        return False

    return check_fields_match(fields["Description"]["name"],
                              substruct_fields[0]["field"], item)


def check_fields(src_fields, dest_fields, desc, sec):
    # This function checks for all the fields in a section.  If some
    # fields got embedded into a substruct, this function will also
    # attempt to check inside the substruct.

    d_iter = iter(dest_fields)
    s_iter = iter(src_fields)

    # Using these lists as stacks to store previous value of s_iter
    # and d_iter, so that when time comes to exit out of a substruct,
    # we can go back one level up and continue from where we left off.

    s_iter_list = []
    d_iter_list = []

    advance_src = True
    advance_dest = True
    unused_count = 0

    while True:
        if advance_src:
            try:
                s_item = s_iter.next()
            except StopIteration:
                if s_iter_list == []:
                    break

                s_iter = s_iter_list.pop()
                continue
        else:
            if unused_count == 0:
                # We want to avoid advancing just once -- when entering a
                # dest substruct, or when exiting one.
                advance_src = True

        if advance_dest:
            try:
                d_item = d_iter.next()
            except StopIteration:
                if d_iter_list == []:
                    # We were not in a substruct
                    print('Section "' + sec + '", '
                          'Description "' + desc + '": '
                          'expected field "' + s_item["field"] + '", '
                          'while dest has no further fields')
                    bump_taint()
                    break

                d_iter = d_iter_list.pop()
                advance_src = False
                continue
        else:
            if unused_count == 0:
                advance_dest = True

        if unused_count != 0:
            if not advance_dest:
                unused_count = unused_count - s_item["size"]
                if unused_count == 0:
                    advance_dest = True
                    continue
                if unused_count < 0:
                    print('Section "' + sec + '", '
                          'Description "' + desc + '": '
                          'unused size mismatch near "' +
                          s_item["field"] + '"')
                    bump_taint()
                    break
                continue

            if not advance_src:
                unused_count = unused_count - d_item["size"]
                if unused_count == 0:
                    advance_src = True
                    continue
                if unused_count < 0:
                    print('Section "' + sec + '", '
                          'Description "' + desc + '": '
                          'unused size mismatch near "' + d_item["field"] +
                          '"')
                    bump_taint()
                    break
                continue

        if not check_fields_match(desc, s_item["field"], d_item["field"]):
            # Some fields were put in substructs, keeping the
            # on-wire format the same, but breaking static tools
            # like this one.

            # First, check if dest has a new substruct.
            if exists_in_substruct(d_item, s_item["field"]):
                # listiterators don't have a prev() function, so we
                # have to store our current location, descend into the
                # substruct, and ensure we come out as if nothing
                # happened when the substruct is over.
                #
                # Essentially we're opening the substructs that got
                # added which didn't change the wire format.
                d_iter_list.append(d_iter)
                substruct_fields = d_item["Description"]["Fields"]
                d_iter = iter(substruct_fields)
                advance_src = False
                continue

            # Next, check if src has substruct that dest removed
            # (can happen in backward migration: 2.0 -> 1.5)
            if exists_in_substruct(s_item, d_item["field"]):
                s_iter_list.append(s_iter)
                substruct_fields = s_item["Description"]["Fields"]
                s_iter = iter(substruct_fields)
                advance_dest = False
                continue

            if s_item["field"] == "unused" or d_item["field"] == "unused":
                if s_item["size"] == d_item["size"]:
                    continue

                if d_item["field"] == "unused":
                    advance_dest = False
                    unused_count = d_item["size"] - s_item["size"]
                    continue

                if s_item["field"] == "unused":
                    advance_src = False
                    unused_count = s_item["size"] - d_item["size"]
                    continue

            # commit 20daa90a20d, extra field 'config' was added in newer
            # releases there will be a mismatch in the number of fields of
            # irq_state and config it's a known false positive so skip it
            if (desc in ["PCIDevice", "PCIEDevice"]):
                if((s_item["field"] in ["irq_state", "config"]) and
                   (d_item["field"] in ["irq_state", "config"])):
                    break

            # some fields are new some dead, but are not errors.
            if not check_fields_match(desc, s_item["field"], d_item["field"]):
                print('Section "' + sec + '", '
                      'Description "' + desc + '": '
                      'expected field "' + s_item["field"] + '", '
                      'got "' + d_item["field"] + '"; skipping rest')
                bump_taint()
                break

        check_version(s_item, d_item, sec, desc)

        if "Description" not in s_item:
            # Check size of this field only if it's not a VMSTRUCT entry
            check_size(s_item, d_item, sec, desc, s_item["field"])

        check_description_in_list(s_item, d_item, sec, desc)


def check_subsections(src_sub, dest_sub, desc, sec):
    for s_item in src_sub:
        found = False
        for d_item in dest_sub:
            if s_item["name"] != d_item["name"]:
                continue

            found = True
            check_descriptions(s_item, d_item, sec)

        # check the updated properties list before throwing error
        if not found and (not check_updated_properties(desc, s_item["name"])):
            print('Section "' + sec + '", '
                  'Description "' + desc + '": '
                  'Subsection "' + s_item["name"] + '" not found')
            bump_taint()


def check_description_in_list(s_item, d_item, sec, desc):
    if "Description" not in s_item:
        return

    if "Description" not in d_item:
        print('Section "' + sec + '", '
              'Description "' + desc + '", '
              'Field "' + s_item["field"] + '": missing description')
        bump_taint()
        return

    check_descriptions(s_item["Description"], d_item["Description"], sec)


def check_descriptions(src_desc, dest_desc, sec):
    check_version(src_desc, dest_desc, sec, src_desc["name"])

    if not check_fields_match(sec, src_desc["name"], dest_desc["name"]):
        print('Section "' + sec + '": '
              'Description "' + src_desc["name"] + '" '
              'missing, got "' + dest_desc["name"] + '" instead; skipping')
        bump_taint()
        return

    for field in src_desc:
        if field not in dest_desc:
            # check the updated list of changed properties
            # before throwing error
            if check_updated_properties(src_desc["name"], field):
                continue
            else:
                print('Section "' + sec + '" '
                      'Description "' + src_desc["name"] + '": '
                      'Entry "' + field + '" missing')
                bump_taint()
                continue

        if field == 'Fields':
            check_fields(src_desc[field], dest_desc[field],
                         src_desc["name"], sec)

        if field == 'Subsections':
            check_subsections(src_desc[field], dest_desc[field],
                              src_desc["name"], sec)


def check_version(src_ver, dest_ver, sec, desc=None):
    if src_ver["version_id"] > dest_ver["version_id"]:
        if not check_updated_sizes(sec, src_ver["version_id"],
                                   dest_ver["version_id"]):
            print ('Section "' + sec + '"'),
            if desc:
                print ('Description "' + desc + '":'),
            print ('version error: ' + str(src_ver["version_id"]) + ' > ' +
                   str(dest_ver["version_id"]))
            bump_taint()

    if "minimum_version_id" not in dest_ver:
        return

    if src_ver["version_id"] < dest_ver["minimum_version_id"]:
        if not check_updated_sizes(sec, src_ver["version_id"],
                                   dest_ver["minimum_version_id"]):
            print ('Section "' + sec + '"'),
            if desc:
                print('Description "' + desc + '": ' +
                      'minimum version error: ' + str(src_ver["version_id"]) +
                      ' < ' + str(dest_ver["minimum_version_id"]))
                bump_taint()


def check_size(src, dest, sec, desc=None, field=None):
    if src["size"] != dest["size"]:
        # check updated sizes list before throwing error

        if not check_updated_sizes(field, src["size"], dest["size"]):
            print ('Section "' + sec + '"'),
            if desc:
                print ('Description "' + desc + '"'),
            if field:
                print ('Field "' + field + '"'),
            print ('size mismatch: ' + str(src["size"]) + ' , ' +
                   str(dest["size"]))
            bump_taint()


def check_machine_type(src, dest):
    if src["Name"] != dest["Name"]:
        print('Warning: checking incompatible machine types: '
              '"' + src["Name"] + '", "' + dest["Name"] + '"')
    return


def main():
    help_text = ("Parse JSON-formatted vmstate dumps from QEMU in files "
    "SRC and DEST.  Checks whether migration from SRC to DEST QEMU versions "
    "would break based on the VMSTATE information contained within the JSON "
    "outputs. The JSON output is created from a QEMU invocation with the "
    "-dump-vmstate parameter and a filename argument to it. Other parameters "
    " to QEMU do not matter, except the -M (machine type) parameter.")

    parser = argparse.ArgumentParser(description=help_text)
    parser.add_argument('-s', '--src', type=file, required=True,
                        help='json dump from src qemu')
    parser.add_argument('-d', '--dest', type=file, required=True,
                        help='json dump from dest qemu')
    parser.add_argument('--reverse', required=False, default=False,
                        action='store_true',
                        help='reverse the direction')
    args = parser.parse_args()

    src_data = json.load(args.src)
    dest_data = json.load(args.dest)
    args.src.close()
    args.dest.close()

    if args.reverse:
        temp = src_data
        src_data = dest_data
        dest_data = temp

    for sec in src_data:
        dest_sec = sec
        if dest_sec not in dest_data:
            # Either the section name got changed, or
            # the section doesn't exist in dest or
            # section was newly supported.
            dest_sec = get_changed_sec_name(sec)
            if dest_sec == "":
                if not check_new_sections(sec):
                    # section not in newly supported list and not in dest
                    print('Section "' + sec + '" does not exist in dest')
                    bump_taint()
                continue
            else:
                # section name changed
                if dest_sec not in dest_data:
                    # new name not found in dest.
                    if check_new_sections(dest_sec):
                        continue
                    else:
                        # new name not in dest and not newly supported
                        print('Section "' + sec + '" does not exist in dest')
                        bump_taint()
                        continue

        s = src_data[sec]
        d = dest_data[dest_sec]

        if sec == "vmschkmachine":
            check_machine_type(s, d)
            continue

        check_version(s, d, sec)

        for entry in s:
            if entry not in d:
                print('Section "' + sec + '": Entry "' + entry + '" missing')
                bump_taint()
                continue

            if entry == "Description":
                check_descriptions(s[entry], d[entry], sec)

    return taint


if __name__ == '__main__':
    sys.exit(main())
