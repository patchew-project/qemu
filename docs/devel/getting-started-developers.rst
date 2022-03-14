.. _getting-started-developers:

Getting Started for Developers
==============================

You want to contribute code, documentation or patches to QEMU?

Then...

-  ... you should probably first join the :ref:`mailing-lists`.

   -  Mailing lists are moderated. Non-subscribers may post, so list
      policy is reply-to-all to ensure original poster is included.
   -  Be prepared for upwards of one thousand messages per week if you
      subscribe.
   -  First-time posts (whether subscribed or not) are subject to a
      moderation delay until a human can whitelist your email address.

-  Also check out the `patch
   submission <https://www.qemu.org/docs/master/devel/submitting-a-patch.html>`__
   page for some hints on mail contributions.

Wiki
----

-  To create an account in the QEMU wiki, you must ask on the mailing
   list for someone else to do it on your behalf (self-creation is
   prohibited to cut down on spam accounts).
-  Start with reading the QEMU wiki.
-  Contribute to the QEMU wiki by adding new topics or improving and
   expanding existing topics. It should help you and others in the
   future.

Documentation
-------------

-  Continue with reading the `existing documentation <Documentation>`__
   and `Contributions Guide <Contribute>`__.

   Be prepared that all written documentation might be invalid - either
   because it is too old or because it was never correct. And it is
   never complete...

-  If you find bugs in the documentation then fix them and send patches
   to the mailing list. See
   `Contribute/ReportABug <Contribute/ReportABug>`__.
-  If you find problems in the wiki, then fix them if you can, or add
   notes to either the applicable page or the Discussion page.
-  Depending on how much computer architecture / hardware background you
   have, it may help to read some general books. Suggestions include:

   -  "Computers as Components, Second Edition: Principles of Embedded
      Computing System Design", Wayne Wolf, ISBN-13: 978-0123743978

Code
----

-  Get the code. If you want to be a developer, you almost certainly
   want to be building from git (see the
   `Download <http://www.qemu-project.org/download/#source>`__ page for
   the right tree).
-  Compile the code. Here are some instructions how to do this:

   -  `QEMU on Linux hosts <Hosts/Linux>`__
   -  `QEMU on OS X (macOS) hosts <Hosts/Mac>`__
   -  `QEMU on Windows hosts <Hosts/W32>`__

-  Run the QEMU system and user mode emulation for different targets
   (x86, mips, powerpc, ...). Images can be obtained from the
   `Testing <Testing>`__ page.
-  QEMU has a lot of different parts (hardware device emulation, target
   emulation, code generation for different hosts, configuration, ...).

   -  Choose an interesting part and concentrate on it for some time and
      read the code. Its going to take some effort, so try to find
      something that you are really interested in - something that will
      be a least a little bit fun for you.
   -  It will be easier if you choose a part of the code that has an
      active / responsive maintainer (since this gives you someone to
      discuss things with).

-  If you find bugs in the code, then fix them and send a patch to the
   mailing list (see `patch submission
   process <https://www.qemu.org/docs/master/devel/submitting-a-patch.html>`__)

   -  Patches need to go the mailing list, and possibly also to a
      specific maintainer (read the MAINTAINERS text file in the top of
      the source code tree).
   -  Read the HACKING and CODING_STYLE text files (in the top of the
      source code tree) before writing the patch
   -  Run your patch through the . See
      http://blog.vmsplice.net/2011/03/how-to-automatically-run-checkpatchpl.html
      for how to hook it into git.
   -  For very small, simple changes, you can do it as a single patch.
      If your change is more complex, you need to break it into smaller,
      separate patches (which together form a set of patches, or a
      patchset). Each step in the patch process can rely on previous
      patches, but not later patches - otherwise "git bisect" will
      break. This will require more effort on your part, but it saves a
      lot of work for everyone else.
   -  If you have a lot of patches in a patchset (say five or more),
      then it may help to have a public git tree. You can get hosting
      from many places (repo.or.cz and github seem popular).

.. _getting_to_know_the_code:

Getting to know the code
------------------------

-  QEMU does not have a high level design description document - only
   the source code tells the full story.
-  There are some useful (although usually dated) descriptions for
   infrastructure code in various parts of the wiki, and sometimes in
   mailing list descriptions:

   -  Tracing framework is described at
      `Features/Tracing <Features/Tracing>`__ and in docs/tracing.txt in
      the source tree.
   -  Some of the internal functionality (and a bit of the human
      monitoring / control interface) is implemented in
      `QAPI <Features/QAPI>`__ and `QMP <Documentation/QMP>`__. See also
      https://www.linux-kvm.org/images/1/17/2010-forum-qmp-status-talk.pp.pdf
   -  If you're into devices (new virtual hardware) it will help to know
      about QDEV:
      http://www.linux-kvm.org/images/f/fe/2010-forum-armbru-qdev.pdf
      and docs/qdev-device-use.txt in the source tree

-  Things do change -- we improve our APIs and develop better ways of
   doing things all the time. Reading the mailing list is important to
   keep on top of these changes. You may also find the
   `DeveloperNews <DeveloperNews>`__ wiki page a useful summary. We try
   to track API and design changes currently in progress on the
   `ToDo/CodeTransitions <ToDo/CodeTransitions>`__ page; this may help
   you avoid accidentally copying existing code which is out-of-date or
   no longer following best practices.

   -  We also maintain a list of
      `Contribute/BiteSizedTasks <Contribute/BiteSizedTasks>`__ that can
      help

getting familiar with the code, and complete those transitions to make
things easier for the next person!

-  QEMU converts instructions in the guest system into instructions on
   the host system via Tiny Code Generator (TCG). See tcg/README in the
   source tree as a starting point if you want to understand this.
-  Some of QEMU makes extensive use of pre-processor operations
   (especially token pasting with ## operation) which can make it harder
   to determine where a particular function comes from. Mulyadi Santosa
   pointed out that you can use the gcc "--save-temps" option to see the
   results of the pre-processor stage - see
   http://the-hydra.blogspot.com/2011/04/getting-confused-when-exploring-qemu.html
   for more detail.

Bugs
----

-  Read the Bug Tracker.
-  Check for topics in it for which you feel capable of handling and try
   to fix the issue. Send patches.

.. _testing_your_changes:

Testing your changes
--------------------

-  You must compile test for all targets (i.e. don't restrict the
   target-list at configure time). Make sure its a clean build if you
   are not sure.
-  Think about what you've changed (review the patches) and do testing
   consistent with those changes. For example:

   -  If you've developed a new driver (say an AHCI virtual device -
      used for SATA disks), make sure that it works. Make sure anything
      related still works (e.g. for AHCI, check the IDE driver, and no
      disk configurations). If your new device supports migration, make
      sure migration and snapshots work.
   -  If you're working on Xen Hardware Virtual Machine (HVM - full
      virtualization), then make sure that Xen para-virtualization
      works.

-  Its OK if your new implementation doesn't do everything (or has some
   deficiencies / bugs). You do need to declare that in the patch
   submission though.
-  Main page for testing resources: `Testing <Testing>`__

.. _getting_help:

Getting Help
------------

-  IRC might be useful

   -  The #qemu channel is on irc://irc.oftc.net (note: no longer on
      Freenode).
   -  You may also get a response on the #kvm channel on
      irc://irc.freenode.net

Please don't post large text dumps on IRC. Use a pastebin service to
post logs so the channel doesn't get flooded.
