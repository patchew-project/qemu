==
CI
==

QEMU has configurations enabled for a number of different CI services.
The most up to date information about them and their status can be
found at::

   https://wiki.qemu.org/Testing/CI

Jobs on Custom Runners
======================

Besides the jobs run under the various CI systems listed before, there
are a number additional jobs that will run before an actual merge.
These use the same GitLab CI's service/framework already used for all
other GitLab based CI jobs, but rely on additional systems, not the
ones provided by GitLab as "shared runners".

The architecture of GitLab's CI service allows different machines to
be set up with GitLab's "agent", called gitlab-runner, which will take
care of running jobs created by events such as a push to a branch.
Here, the combination of a machine, properly configured with GitLab's
gitlab-runner, is called a "custom runner".

The GitLab CI jobs definition for the custom runners are located under::

  .gitlab-ci.d/custom-runners.yml

Machine Setup Howto
-------------------

For all Linux based systems, the setup can be mostly automated by the
execution of two Ansible playbooks.  Create an ``inventory`` file
under ``scripts/ci/setup``, such as this::

  fully.qualified.domain
  other.machine.hostname

You may need to set some variables in the inventory file itself.  One
very common need is to tell Ansible to use a Python 3 interpreter on
those hosts.  This would look like::

  fully.qualified.domain ansible_python_interpreter=/usr/bin/python3
  other.machine.hostname ansible_python_interpreter=/usr/bin/python3

Build environment
~~~~~~~~~~~~~~~~~

The ``scripts/ci/setup/build-environment.yml`` Ansible playbook will
set up machines with the environment needed to perform builds and run
QEMU tests.  It covers a number of different Linux distributions and
FreeBSD.

To run the playbook, execute::

  cd scripts/ci/setup
  ansible-playbook -i inventory build-environment.yml
