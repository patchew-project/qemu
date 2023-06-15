.. _tarball-release-process:

QEMU source tarballs and the release management process
=======================================================

Overview
--------

Once an official release is tagged in the QEMU git tree by the current
upstream maintainer (for major releases), or current stable maintainer
(for stable releases), additional work is needed to generate/publish a
source tarball for consumption by distros, vendors, and end-users who
may not use git sources directly. A person involved in producing these
tarballs is referred to herein as a "release maintainer" (not to be
confused with the upstream/stable maintainers who are responsible for
managing/tagging the git trees from which the tarballs are generated).

This documents provides an overview of this release process and is
mainly intended as a reference for current/prospective release maintainers
and other individuals involved in the release management process, but it
may also be useful to consumers of these source tarballs.

Generating source tarballs
--------------------------

The following example describes the tarball creation process for a
particular tagged commit in the QEMU source tree, in this case v8.0.0:

  .. code::

     # Check out the tagged commit
     push ${qemu_git_dir}
     git checkout v8.0.0
     popd

     # Generate the .bz2 tarball
     mkdir ${qemu_build_dir}
     cd ${qemu_build_dir}
     ${qemu_git_dir}/configure
     make qemu-8.0.0.tar.bz2

     # Generate the .xz tarball
     bzip2 -k -d qemu-8.0.0.tar.bz2
     xz -9 -k qemu-8.0.0.tar

     # Sign the resulting tarballs
     gpg -b qemu-8.0.0.tar.bz2
     gpg -b qemu-8.0.0.tar.xz

Testing source tarballs
-----------------------

While releases tagged in the QEMU git tree will have undergone the full
range of CI testing already, the scripts/process to generate the source
tree contained in a tarball warrant some additional testing to guard
against regressions being introduced at this stage in the process. At a
minimum, an all-target build of QEMU using features commonly enabled by
distros should be performed, e.g.:

  .. code::

     tar jxvf qemu-8.0.0.tar.bz2
     mkdir qemu-8.0.0-build
     cd qemu-8.0.0-build
     ../qemu-8.0.0/configure --extra-cflags=-Wall \
        --enable-gtk --enable-numa --enable-linux-aio --enable-usb-redir \
        --enable-virtfs --enable-libusb
     make
     make check

Publishing source tarballs
--------------------------

The publishing process generally involves the following steps:

1) Upload the tarballs and their corresponding signatures to qemu.org's
   file host. This process may change occasionally due to qemu.org's
   changing bandwidth/infrastructure needs, so check with the QEMU team on
   setting up access and getting details on the specific upload process. One
   fairly constant requirement however is to upload all the required
   components to the appropriate location, e.g.:

  .. code::

     qemu-8.0.0.tar.bz2
     qemu-8.0.0.tar.bz2.sig
     qemu-8.0.0.tar.xz
     qemu-8.0.0.tar.xz.sig

2) Update the links on the qemu.org download page. This is currently
   handled automatically when pushing updates to the git repo used to
   manage the content on qemu.org. An example of the commit
   corresponding to publishing the QEMU 8.0.0 release can be seen here:

   https://gitlab.com/qemu-project/qemu-web/-/commit/8a2082e67c1b39d41bd526bfa0435de34199d6d9

   Prior to a final release there are usually a number of release
   candidates tarballs which are also published on qemu.org. An example for
   QEMU 8.0.0-rc4 can be seen here:

   https://gitlab.com/qemu-project/qemu-web/-/commit/c976cbe248ca76eae9e938f718372a2dba1a21af

3) Send an announcement to the QEMU mailing list. The format may differ
   slightly for major releases, RCs, and stable stable releases. It may
   also change a bit based on the preferences of the current release
   maintainer or other factors.

   For reference, an example announcement for the QEMU 8.0.0 major release
   can be found here:

   https://lists.nongnu.org/archive/html/qemu-devel/2023-04/msg02755.html

   An example announcement for the QEMU 8.0.0-rc4 major release candidate
   can be found here:

   https://lists.nongnu.org/archive/html/qemu-devel/2023-04/msg02026.html

   An example announcement for the QEMU 8.0.2 stable release can be found
   here:

   https://lists.nongnu.org/archive/html/qemu-devel/2023-06/msg00221.html

4) For major releases, add an accompanying blog entry to the git repo used
   to manage qemu.org. It should generally follow the format of the
   above-mentioned announcement sent to the QEMU mailing list. Generally
   this is only done for final releases, and not for RCs, e.g.:

   https://gitlab.com/qemu-project/qemu-web/-/commit/a5cb9e1a81e46b7e431ec3a0c130d0b4bf93d39a

   For stable releases there generally isn't an associated blog entry, but
   providing one may be worthwhile in some cases for bringing additional
   attention to releases that address critical functional/security issues.
