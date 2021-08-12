CI Job Rules
============

The following documents how CI rules control execution of jobs in a pipeline

Job variable naming
-------------------

The rules for controlling jobs in a pipeline will be driven by a number of
variables:

 - ``CI_XXX``, ``GITLAB_XXX`` - variables defined by GitLab:

   https://docs.gitlab.com/ee/ci/variables/predefined_variables.html

 - ``QEMU_JOB_XXX`` - variables defined in the YAML files that express
   characteristics of the job used to control default behaviour

 - ``QEMU_CI_XXX`` - variables defined by the user that are used to fine
   tune which jobs are run dynamically

and in some cases based on the branch name prefixes.

Job fine tuning strategies
--------------------------

Using a combination of the ``QEMU_CI_XXX`` variables and ``ci-``
branch name prefix, users can fine tune what jobs are run.

Set variable globally in the gitlab repository CI config
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Variables can be set globally in the user's gitlab repository CI config.
These variables will apply to all pipelines associated with the repository
thereafter. This is useful for fine tuning the jobs indefinitely.

For further information about how to set these variables, please refer to::

  https://docs.gitlab.com/ee/ci/variables/#add-a-cicd-variable-to-a-project

Set variable manually when pushing to git
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Variables can be set manually when pushing a branch or tag, using
git-push command line arguments. This is useful for fine tuning the
jobs on an adhoc basis.

Example setting the QEMU_CI_EXAMPLE_VAR variable:

.. code::

   git push -o ci.variable="QEMU_CI_EXAMPLE_VAR=value" myrepo mybranch

For further information about how to set these variables, please refer to::

  https://docs.gitlab.com/ee/user/project/push_options.html#push-options-for-gitlab-cicd

Pushing to specially named branches
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Branch names starting with a 'ci-' prefix can be used as an alternative
to setting variables. Details of supported branch names are detailed
later in this document.

Provide a custom gitlab CI configuration
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

The above strategies all provide a way to fine tune the jobs defined by the
standard QEMU gitlab CI configuration file. If this is not sufficient it is
possible to completely replace the CI configuration file with a custom
version. This allows the contributor to achieve essentially anything they
desire, within the scope of what GitLab supports.

Replacing the ``.gitlab-ci.yml`` file is done in the repository settings:

  https://docs.gitlab.com/ee/ci/pipelines/settings.html#specify-a-custom-cicd-configuration-file

Note that it is possible to reference an arbitrary URL. This could point
to a replacement .gitlab-ci.yml on a specific branch in the same repo,
or can point to any external site.


Job grouping sets
-----------------

There are many different jobs defined in the GitLab CI pipeline used by QEMU.
It is not practical to run all jobs in every scenario, so there are a set of
rules defined that control which jobs are executed on each pipeline run. At
a high level the jobs can be grouped into a number of mutually exclusive
sets.

 - Manual jobs

   This is a set of jobs that will never be run automatically in any scenario.
   The reason a job will be marked manual is if it is known to exhibit some
   non-deterministic failures, or liable to trigger timeouts. Ideally jobs are
   only present in this set for a short period of time while the problems are
   investigated and resolved. Users can manually trigger these jobs from the
   the pipelines web UI if desired, but they will never contribute to the
   overall pipeline status.

   Identified by the variable ``QEMU_JOB_MANUAL: 1``


 - Minimal jobs

   This is a minimal set of jobs that give a reasonable build and test sanity
   check, which will be run by default in all possible scenarios. This set of
   jobs is intended to be controlled fairly strictly to avoid wasting CI
   minutes of maintainers/contributors. The intent is to get 80-90% coverage
   of build and unit tests across the most important host platforms and
   architectures.

   Identified by the variable ``QEMU_JOB_MINIMAL: 1``

   Run by setting the variable ``QEMU_CI: 1`` or pushing to a branch
   named ``ci-min-XXX``.


 - Gating jobs

   This is a set of jobs will will always run as a gating test prior to code
   merging into the default branch of the primary QEMU git repository. In user
   forks the jobs will not run by default, but the user can opt-in to trigger
   their execution. These jobs may run a particularly thorough set of scenarios
   that maintainers are not normally going to exercise before sending series.

   Identified by the variable ``QEMU_JOB_GATING: 1``

   Run by setting the variable ``QEMU_CI_GATING: 1`` or pushing to a branch
   named ``ci-gating-XXX``


 - Optional jobs

   This is all remaining jobs that don't fall into one of the above buckets.
   These jobs will always be run as a gating test prior to code merging into
   the default branch of the primary QEMU git repository. In user forks the
   jobs will not run by default, but the user can opt-in to trigger their
   execution. These jobs will aim to expand the host/platform build coverage
   gaps left by the default jobs.

   Identified by not having any of the above variables set.

   Run by setting the variable ``QEMU_CI_ALL: 1`` or pushing to a branch
   named ``ci-all-XXXX``

Since the sets are mutually exclusive, if more than one of the variables above
is set, then only one of them will be honoured. The precedence matches the
ordering of the above docs.

In addition to the above, there are some special extra sets that can augment
any of the above sets

 - Publishing jobs

   These are jobs that are needed to publish the QEMU website. For user fork
   repos these follow the normal rules set out above. For the primary QEMU
   repo, however, the jobs will also run on the default branch.

   Identified by the variable ``QEMU_JOB_PUBLISH: 1``
