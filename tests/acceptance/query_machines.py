# Sanity check query-machines QMP command results
#
# Copyright (c) 2019 Red Hat, Inc.
#
# Author:
#  Eduardo Habkost <ehabkost@redhat.com>
#
# This work is licensed under the terms of the GNU GPL, version 2 or
# later.  See the COPYING file in the top-level directory.

from avocado_qemu import Test

class QueryMachines(Test):
    def test(self):
        self.vm.launch()
        machines = self.vm.command('query-machines')
        machinesdict = dict((m['name'], m) for m in machines)
        machinesdict.update((m['alias'], m) for m in machines if 'alias' in m)
        for machine in machines:
            status = machine['support-status']
            if status['deprecated']:
                self.assertTrue(status.get('status-message') or \
                                status.get('suggested-alternative'),
                                "Deprecated machine (%s) must have status-message or suggested-alternative" % (machine['name']))
            if 'suggested-alternative' in status:
                self.assertTrue(status['suggested-alternative'] in machinesdict,
                                "suggested-alternative of %s must point to a valid machine type" % (machine['name']))
