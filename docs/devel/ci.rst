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
