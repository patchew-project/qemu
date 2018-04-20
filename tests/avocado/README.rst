This directory is hosting functional tests written using Avocado Testing
Framework. To install Avocado, follow the instructions from this link::

    http://avocado-framework.readthedocs.io/en/latest/GetStartedGuide.html#installing-avocado

Tests here are written keeping the minimum amount of dependencies. To
run the tests, you need the Avocado core package (`python-avocado` on
Fedora, `avocado-framework` on pip). Extra dependencies should be
documented in this file.

In this directory, an ``avocado_qemu`` package is provided, containing
the ``test`` module, which inherits from ``avocado.Test`` and provides
a builtin and easy-to-use Qemu virtual machine. Here's a template that
can be used as reference to start writing your own tests::

    from avocado_qemu import test

    class MyTest(test.QemuTest):
        """
        :avocado: enable
        """

        def setUp(self):
            self.vm.args.extend(['-m', '512'])
            self.vm.launch()

        def test_01(self):
            res = self.vm.qmp('human-monitor-command',
                              command_line='info version')
            self.assertIn('v2.9.0', res['return'])

        def tearDown(self):
            self.vm.shutdown()

To execute your test, run::

    avocado run test_my_test.py

To execute all tests, run::

    avocado run .

If you don't specify the Qemu binary to use, the ``avocado_qemu``
package will automatically probe it. The probe will try to use the Qemu
binary from the git tree build directory, using the same architecture as
the local system (if the architecture is not specified). If the Qemu
binary is not available in the git tree build directory, the next try is
to use the system installed Qemu binary.

You can define a number of optional parameters, providing them via YAML
file using the Avocado parameters system:

- ``qemu_bin``: Use a given Qemu binary, skipping the automatic
  probe. Example: ``qemu_bin: /usr/libexec/qemu-kvm``.
- ``qemu_dst_bin``: Use a given Qemu binary to create the destination VM
  when the migration process takes place. If it's not provided, the same
  binary used in the source VM will be used for the destination VM.
  Example: ``qemu_dst_bin: /usr/libexec/qemu-kvm-binary2``.
- ``arch``: Probe the Qemu binary from a given architecture. It has no
  effect if ``qemu_bin`` is specified. If not provided, the binary probe
  will use the system architecture. Example: ``arch: x86_64``
- ``image_path``: VMs are defined without image. If the ``image_path``
  is specified, it will be used as the VM image. The ``-snapshot``
  option will then be used to avoid writing into the image. Example:
  ``image_path: /var/lib/images/fedora-25.img``
- ``image_user`` and ``image_pass``: When using a ``image_path``, if you
  want to get the console from the Guest OS you have to define the Guest
  OS credentials. Example: ``image_user: root`` and
  ``image_pass: p4ssw0rd``
- ``machine_type``: Use this option to define a machine type for the VM.
  Example: ``machine_type: pc``
- ``machine_accel``: Use this option to define a machine acceleration
  for the VM. Example: ``machine_accel: kvm``.
- ``machine_kvm_type``: Use this option to select the KVM type when the
  ``accel`` is ``kvm`` and there are more than one KVM types available.
  Example: ``machine_kvm_type: PR``

To use a parameters file, you have to install the yaml_to_mux plugin
(`python2-avocado-plugins-varianter-yaml-to-mux` on Fedora,
`avocado-framework-plugin-varianter-yaml-to-mux` on pip).

Run the test with::

    $ avocado run test_my_test.py -m parameters.yaml

Additionally, you can use a variants file to to set different values
for each parameter. Using the YAML tag ``!mux`` Avocado will execute the
tests once per combination of parameters. Example::

    $ cat variants.yaml
    architecture: !mux
        x86_64:
            arch: x86_64
        i386:
            arch: i386

Run it the with::

    $ avocado run test_my_test.py -m variants.yaml

You can use both the parameters file and the variants file in the same
command line::

    $ avocado run test_my_test.py -m parameters.yaml variants.yaml

Avocado will then merge the parameters from both files and create the
proper variants.

See ``avocado run --help`` and ``man avocado`` for several other
options, such as ``--filter-by-tags``, ``--show-job-log``,
``--failfast``, etc.
