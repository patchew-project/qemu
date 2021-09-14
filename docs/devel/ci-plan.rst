The GitLab CI structure
=======================

This section describes the current state of the QEMU GitLab CI and the
high-level plan for its future.

Current state
-------------

The mainstream QEMU project considers the GitLab CI its primary CI system.
Currently, it runs 120+ jobs, where ~36 are container build jobs, 69 are QEMU
build jobs, ~22 are test jobs, 1  is a web page deploy job, and 1 is an
external job covering Travis jobs execution.

In the current state, every push a user does to its fork runs most of the jobs
compared to the jobs running on the main repository. The exceptions are the
acceptance tests jobs, which run automatically on the main repository only.
Running most of the jobs in the user's fork or the main repository is not
viable. The job number tends to increase, becoming impractical to run all of
them on every single push.

Future of QEMU GitLab CI
------------------------

Following is a proposal to establish a high-level plan and set the
characteristics for the QEMU GitLab CI. The idea is to organize the CI by use
cases, avoid wasting resources and CI minutes, anticipating the time GitLab
starts to enforce minutes limits soon.

Use cases
^^^^^^^^^

Below is a list of the most common use cases for the QEMU GitLab CI.

Gating
""""""

The gating set of jobs runs on the maintainer's pull requests when the project
leader pushes them to the staging branch of the project. The gating CI pipeline
has the following characteristics:

 * Jobs tagged as gating run as part of the gating CI pipeline;
 * The gating CI pipeline consists of stable jobs;
 * The execution duration of the gating CI pipeline should, as much as possible,
   have an upper bound limit of 2 hours.

Developers
""""""""""

A developer working on a new feature or fixing an issue may want to run/propose
a specific set of tests. Those tests may, eventually, benefit other developers.
A developer CI pipeline has the following characteristics:

 * It is easy to run current tests available in the project;
 * It is easy to add new tests or remove unneeded tests;
 * It is flexible enough to allow changes in the current jobs.

Maintainers
"""""""""""

When accepting developers' patches, a maintainer may want to run a specific
test set. A maintainer CI pipeline has the following characteristics:

 * It consists of tests that are valuable for the subsystem;
 * It is easy to run a set of specific tests available in the project;
 * It is easy to add new tests or remove unneeded tests.

Scheduled / periodic pipelines
""""""""""""""""""""""""""""""

The scheduled CI pipeline runs periodically on the master/main branch of the
project. It covers as many jobs as needed or allowed by the execution duration
of GitLab CI. The main idea of this pipeline is to run jobs that are not part
of any other use cases due to some limitations, like execution duration, or
flakiness. This pipeline may be helpful, for example, to collect test/job
statistics or to define test/job stability. The scheduled CI pipeline should
not act as a gating CI pipeline.
