import logging
import time
import distutils.spawn

from avocado import skipUnless
from avocado_qemu import Test
from avocado.utils import process

class AVR6Machine(Test):
    timeout = 5

    def test_freertos(self):
        """
        :avocado: tags=arch:avr
        :avocado: tags=machine:sample
        """
        """
        https://github.com/seharris/qemu-avr-tests/raw/master/free-rtos/Demo/AVR_ATMega2560_GCC/demo.elf
        constantly prints out 'ABCDEFGHIJKLMNOPQRSTUVWXABCDEFGHIJKLMNOPQRSTUVWX'
        """
        rom_url = 'https://github.com/seharris/qemu-avr-tests'
        rom_url += '/raw/master/free-rtos/Demo/AVR_ATMega2560_GCC/demo.elf'
        rom_hash = '7eb521f511ca8f2622e0a3c5e8dd686efbb911d4'
        rom_path = self.fetch_asset(rom_url, asset_hash=rom_hash)

        self.vm.set_machine('sample')
        self.vm.add_args('-bios', rom_path)
        self.vm.add_args('-nographic')
        self.vm.launch()

        time.sleep(2)
        self.vm.shutdown()

        match = 'ABCDEFGHIJKLMNOPQRSTUVWXABCDEFGHIJKLMNOPQRSTUVWX'

        self.assertIn(match, self.vm.get_log())
