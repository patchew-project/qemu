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
gitlab-runner, is called a "custom runner" here.

The GitLab CI jobs definition for the custom runners are located under::

  .gitlab-ci.d/custom-runners.yml

Current Jobs
------------

The current CI jobs based on custom runners have the primary goal of
catching and preventing regressions on a wider number of host systems
than the ones provided by GitLab's shared runners.

Also, the mechanics of reliability, capacity and overall maintanance
of the machines provided by the QEMU project itself for those jobs
will be evaluated.

Future Plans and Jobs
---------------------

Once the CI Jobs based on custom runners have been proved mature with
the initial set of jobs run on machines from the QEMU project, other
members in the community should be able provide their own machine
configuration documentation/scripts, and accompanying job definitions.

As a general rule, those newly added contributed jobs should run as
"non-gating", until their reliability is verified.

The precise minimum requirements and exact rules for machine
configuration documentation/scripts, and the success rate of jobs are
still to be defined.

Machine Setup Howto
-------------------

For all Linux based systems, the setup can be mostly automated by the
execution of two Ansible playbooks.  Start by adding your machines to
the ``inventory`` file under ``scripts/ci/setup``, such as this::

  [local]
  fully.qualified.domain
  other.machine.hostname

You may need to set some variables in the inventory file itself.  One
very common need is to tell Ansible to use a Python 3 interpreter on
those hosts.  This would look like::

  [local]
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
