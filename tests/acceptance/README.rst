==============================================
 Acceptance tests using the Avocado Framework
==============================================

This directory hosts functional tests, also known as acceptance level
tests.  They're usually higher level, and may interact with external
resources and with various guest operating systems.

The tests are written using the Avocado Testing Framework, in
conjunction with a the ``avocado_qemu.Test`` class, distributed here.

Installation
============

To install Avocado and its dependencies, run::

  pip install --user avocado-framework

Alternatively, follow the instructions on this link:

  http://avocado-framework.readthedocs.io/en/latest/GetStartedGuide.html#installing-avocado

Overview
========

This directory provides the ``avocado_qemu`` Python module, containing
the ``avocado_qemu.Test`` class.  Here's a simple usage example::

  from avocado_qemu import Test


  class Version(Test):
      """
      :avocado: enable
      :avocado: tags=quick
      """
      def test_qmp_human_info_version(self):
          self.vm.launch()
          res = self.vm.command('human-monitor-command',
                                command_line='info version')
          self.assertRegexpMatches(res, r'^(\d+\.\d+\.\d)')

To execute your test, run::

  avocado run test_version.py

To run all tests in the current directory, tagged in a particular way,
run::

  avocado run -t <TAG> .

The ``avocado_qemu.Test`` base test class
=========================================

The ``avocado_qemu.Test`` class has a number of characteristics that
are worth being mentioned right away.

First of all, it attempts to give each test a ready to use QEMUMachine
instance, available at ``self.vm``.  Because many tests will tweak the
QEMU command line, launching the QEMUMachine (by using ``self.vm.launch()``)
is left to the test writer.

At test "tear down", ``avocado_qemu.Test`` handles the QEMUMachine
shutdown.

QEMUMachine
-----------

The QEMUMachine API should be somewhat familiar to QEMU hackers.  It's
used in the Python iotests, device-crash-test and other Python scripts.

QEMU binary selection
---------------------

The QEMU binary used for the ``self.vm`` QEMUMachine instance will
primarily depend on the value of the ``qemu_bin`` parameter.  If it's
not explicitly set, its default value will be the result of a dynamic
probe in the same source tree.  A suitable binary will be one that
targets the architecture matching host machine.

Based on this description, test writers will usually rely on one of
the following approaches:

1) Set ``qemu_bin``, and use the given binary

2) Do not set ``qemu_bin``, and use a QEMU binary named like
   "${arch}-softmmu/qemu-system-${arch}", either in the current
   working directory, or in the current source tree.

The resulting ``qemu_bin`` value will be preserved in the
``avocado_qemu.Test`` as an attribute with the same name.

Attribute reference
===================

Besides the attributes and methods that are part of the base
``avocado.Test`` class, the following attributes are available on any
``avocado_qemu.Test`` instance.

vm
--

A QEMUMachine instance, initially configured according to the given
``qemu_bin`` parameter.

qemu_bin
--------

The preserved value of the ``qemu_bin`` parameter or the result of the
dynamic probe for a QEMU binary in the current working directory or
source tree.

Parameter reference
===================

To understand how Avocado parameters are accessed by tests, and how
they can be passed to tests, please refer to::

  http://avocado-framework.readthedocs.io/en/latest/WritingTests.html#accessing-test-parameters

Parameter values can be easily seen in the log files, and will look
like the following::

  PARAMS (key=qemu_bin, path=*, default=x86_64-softmmu/qemu-system-x86_64) => 'x86_64-softmmu/qemu-system-x86_64

qemu_bin
--------

The exact QEMU binary to be used on QEMUMachine.

Uninstalling Avocado
====================

If you've followed the installation instructions above, you can easily
uninstall Avocado.  Start by listing the packages you have installed::

  pip list --user

And remove any package you want with::

  pip uninstall <package_name>
