# Check for crash when using memory beyond the available guest processor
# address space.
#
# Copyright (c) 2023 Red Hat, Inc.
#
# Author:
#  Ani Sinha <anisinha@redhat.com>
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
# You should have received a copy of the GNU General Public License
# along with this program.  If not, see <http://www.gnu.org/licenses/>.

from avocado_qemu import QemuSystemTest
import signal

class MemAddrCheck(QemuSystemTest):
    def test_phybits_low_pse36(self):
        """
        :avocado: tags=machine:q35
        :avocado: tags=arch:x86_64

        With pse36 feature ON, a processor has 36 bits of addressing. So it can
        access up to a maximum of 64GiB of memory. Memory hotplug region begins
        at 4 GiB boundary when "above_4g_mem_size" is 0 (this would be true when
        we have 0.5 GiB of VM memory, see pc_q35_init()). This means total
        hotpluggable memory size is 60 GiB. Per slot, we reserve 1 GiB of memory
        for dimm alignment for all newer machines (see enforce_aligned_dimm
        property for pc machines and pc_get_device_memory_range()). That leaves
        total hotpluggable actual memory size of 59 GiB. If the VM is started
        with 0.5 GiB of memory, maxmem should be set to a maximum value of
        59.5 GiB to ensure that the processor can address all memory directly.
        If maxmem is set to 59.6G, QEMU should fail to start with a message that
        says "phy-bits are too low".
        If maxmem is set to 59.5G with all other QEMU parameters identical, QEMU
        should start fine.
        """
        self.vm.add_args('-S', '-machine', 'q35', '-m',
                         '512,slots=1,maxmem=59.6G',
                         '-cpu', 'pentium,pse36=on', '-display', 'none',
                         '-object', 'memory-backend-ram,id=mem1,size=1G',
                         '-device', 'virtio-mem-pci,id=vm0,memdev=mem1')
        self.vm.set_qmp_monitor(enabled=False)
        self.vm.launch()
        self.vm.wait()
        self.assertEquals(self.vm.exitcode(), 1, "QEMU exit code should be 1")
        self.assertRegex(self.vm.get_log(), r'phys-bits too low')

    def test_phybits_ok_pentium_pse36(self):
        """
        :avocado: tags=machine:q35
        :avocado: tags=arch:x86_64

        Setting maxmem to 59.5G and making sure that QEMU can start with the
        same options as the failing case above.
        """
        self.vm.add_args('-machine', 'q35', '-m',
                         '512,slots=1,maxmem=59.5G',
                         '-cpu', 'pentium,pse36=on', '-display', 'none',
                         '-object', 'memory-backend-ram,id=mem1,size=1G',
                         '-device', 'virtio-mem-pci,id=vm0,memdev=mem1')
        self.vm.set_qmp_monitor(enabled=False)
        self.vm.launch()
        self.vm.shutdown()
        self.assertEquals(self.vm.exitcode(), -signal.SIGTERM,
                          "QEMU did not terminate gracefully upon SIGTERM")

    def test_phybits_ok_pentium2(self):
        """
        :avocado: tags=machine:q35
        :avocado: tags=arch:x86_64

        Pentium2 has 36 bits of addressing, so its same as pentium
        with pse36 ON.
        """
        self.vm.add_args('-machine', 'q35', '-m',
                         '512,slots=1,maxmem=59.5G',
                         '-cpu', 'pentium2', '-display', 'none',
                         '-object', 'memory-backend-ram,id=mem1,size=1G',
                         '-device', 'virtio-mem-pci,id=vm0,memdev=mem1')
        self.vm.set_qmp_monitor(enabled=False)
        self.vm.launch()
        self.vm.shutdown()
        self.assertEquals(self.vm.exitcode(), -signal.SIGTERM,
                          "QEMU did not terminate gracefully upon SIGTERM")

    def test_phybits_low_nonpse36(self):
        """
        :avocado: tags=machine:q35
        :avocado: tags=arch:x86_64

        Pentium processor has 32 bits of addressing without pse36 or pae
        so it can access up to 4 GiB of memory. Setting maxmem to 4GiB
        should make QEMU fail to start with "phys-bits too low" message.
        """
        self.vm.add_args('-S', '-machine', 'q35', '-m',
                         '512,slots=1,maxmem=4G',
                         '-cpu', 'pentium', '-display', 'none',
                         '-object', 'memory-backend-ram,id=mem1,size=1G',
                         '-device', 'virtio-mem-pci,id=vm0,memdev=mem1')
        self.vm.set_qmp_monitor(enabled=False)
        self.vm.launch()
        self.vm.wait()
        self.assertEquals(self.vm.exitcode(), 1, "QEMU exit code should be 1")
        self.assertRegex(self.vm.get_log(), r'phys-bits too low')
