# Migration test
#
# Copyright (c) 2019 Red Hat, Inc.
#
# Author:
#  Cleber Rosa <crosa@redhat.com>
#
# This work is licensed under the terms of the GNU GPL, version 2 or
# later.  See the COPYING file in the top-level directory.


from avocado_qemu import Test

from avocado.utils import network
from avocado.utils import wait


class Migration(Test):
    """
    :avocado: enable
    """

    timeout = 10

    @staticmethod
    def migration_completed(vm):
        cmd_result = vm.qmp('query-migrate')
        if cmd_result is not None:
            result = cmd_result.get('return')
            if result is not None:
                return result.get('status') == 'completed'
        return False

    def test_tcp(self):
        source = self.get_vm()
        port = network.find_free_port()
        if port is None:
            self.cancel('Failed to find a free port')
        dest_uri = 'tcp:localhost:%u' % port
        dest = self.get_vm('-incoming', dest_uri)
        dest.launch()
        source.launch()
        source.qmp('migrate', uri=dest_uri)
        wait.wait_for(self.migration_completed, timeout=self.timeout,
                      step=0.1, args=(source,))
