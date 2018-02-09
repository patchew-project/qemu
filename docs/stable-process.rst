QEMU and the stable process
===========================

QEMU stable releases
--------------------

QEMU stable releases are based upon the last released QEMU version
and marked by an additional version number, e.g. 2.10.1. Occasionally,
a four-number version is released, if a single urgent fix needs to go
on top.

Usually, a stable release is only done for the last released version.

What should go into a stable release?
-------------------------------------

Generally, the following patches are considered stable material:
- Patches that fix severe issues, like fixes for CVEs
- Patches that fix regressions

If you think the patch would be important for users of the current release
(or for a distribution picking fixes), it is usually a good candidate
for stable.


How to get a patch into QEMU stable
-----------------------------------

There are various ways to get a patch into stable:

* Preferred: Make sure that the stable maintainers are on copy when you send
  the patch by adding

  .. code::

     Cc: qemu-stable@nongnu.org

   to the patch description. This will make git add the stable maintainers
   on copy when your patch is sent out.

* If a maintainer judges the patch appropriate for stable later on (or you
  notify them), they will add the same line to your patch, meaning that
  the stable maintainers will be on copy on the maintainer's pull request.

* If you judge an already merged patch suitable for stable, send a mail
  to ``qemu-stable@nongnu.org`` with ``qemu-devel@nongnu.org`` and appropriate
  other people (like the patch author or the relevant maintainer) on copy.

Stable release process
----------------------

When the stable maintainers prepare a new stable release, they will prepare
a git branch with a release candidate and send the patches out to
``qemu-devel@nongnu.org`` for review. If any of your patches are included,
please verify that they look fine. You may also nominate other patches that
you think are suitable for inclusion. After review is complete (may involve
more release candidates), a new stable release is made available.
