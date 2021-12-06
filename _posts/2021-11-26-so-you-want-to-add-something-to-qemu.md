---
layout: post
title:  "So you want to add a new feature to QEMU?"
date:   2021-11-26 19:43:45
author: Alex Bennée
categories: [blog, process, development]
---

From time to time I hear of frustrations from potential new
contributors who have tried to get new features up-streamed into the
QEMU repository. After having read [our patch
guidelines](https://qemu.readthedocs.io/en/latest/devel/submitting-a-patch.html)
they post them to [qemu-devel](https://lore.kernel.org/qemu-devel/).
Often the patches sit there seemingly unread and unloved. The
developer is left wandering if they missed out the secret hand shake
required to move the process forward. My hope is that this blog post
will help.

New features != Fixing a bug
----------------------------

Adding a new feature is not the same as fixing a bug. For an area of
code that is supported for Odd Fixes or above there will be a
someone listed in the
[MAINTAINERS](https://gitlab.com/qemu-project/qemu/-/blob/master/MAINTAINERS)
file. A properly configured `git-send-email` will even automatically
add them to the patches as they are sent out. The maintainer will
review the code and if no changes are requested they ensure the 
patch flows through the appropriate trees and eventually makes it into
the master branch.

This doesn't usually happen for new code unless your patches happen to
touch a directory that is marked as maintained. Without a maintainer
to look at and apply your patches how will it ever get merged?

Adding new code to a project is not a free activity. Code that isn't
actively maintained has a tendency to [bit
rot](http://www.catb.org/jargon/html/B/bit-rot.html) and become a drag
on the rest of the code base. The QEMU code base is quite large and
none of the developers are knowledgeable about the all of it. If
features aren't
[documented](https://qemu.readthedocs.io/en/latest/devel/submitting-a-patch.html)
they tend to remain unused as users struggle to enable them. If an
unused feature becomes a drag on the rest of the code base by preventing
re-factoring and other clean ups it is likely to be deprecated.
Eventually deprecated code gets removed from the code base never to be
seen again.

Fortunately there is a way to avoid the ignominy of ignored new features
and that is to become a maintainer of your own code!

The maintainers path
--------------------

There is perhaps an unfortunate stereotype in the open source world of
maintainers being grumpy old experts who spend their time dismissively
rejecting the patches of new contributors. Having done their time in
the metaphorical trenches of the project they must ingest the email
archive to prove their encyclopedic mastery. Eventually they then
ascend to the status of maintainer having completed the dark key
signing ritual.

In reality the process is much more prosaic - you simply need to send
a patch to the MAINTAINERS file with your email address, the areas you
are going to cover and the level of support you expect to give.

I won't pretend there isn't some commitment required when becoming a
maintainer. However if you were motivated enough to write the code for
a new feature you should be up to keeping it running smoothly in the
upstream. The level of effort required is also proportional to the
popularity of the feature - there is a world of difference between
maintaining an individual device and a core subsystem. If the feature
grows in popularity and you find it difficult to keep up with the
maintainer effort then you can always ask for someone else to take
over.

Practically you will probably want to get yourself a
[GitLab](https://gitlab.com/qemu-project/qemu/-/blob/master/MAINTAINERS)
account so you can run the CI tests on your pull requests. While
membership of `qemu-devel` is recommended no one is expecting you to
read every message sent to it as long as you look at those where you
are explicitly Cc'd.

Now if you are convinced to become a maintainer for your new feature
lets discuss how you can improve the chances of getting it merged.

A practically perfect set of patches
------------------------------------

I don't want to repeat all the valuable information from the
submitting patches document but I do want to emphasise the importance
of responding to comments and collecting review and testing tags.
While it usual to expect a maintainer `Reviewed-by` or `Acked-by` tags
for any patches that touches another sub-system there is still the
problem of getting reviews for your brand new code. Fortunately there
is no approved reviewer list for QEMU. The purpose of review is to
show that someone else has at least applied the patches and run the
code. Even if they are not confident in reviewing the source a
`Tested-by` tag gives confidence that the code works.

Any feature that only gets manually tested from time to time is very
likely to break. If you are the only person who knows how to test
something you will be the one left to identify when it broke. For this
reason we have a wide arrange of testing approaches in the source
tree. The guiding principle of our testing system is to make it easy
for *any* developer to run a test from their command line using the
existing build system. There are two types of test that are probably
most important for a new feature.

The qtest target (`make check-qtest-ARCH`) invokes a device emulation
testing framework that involve starting an instance of QEMU and then
controlling it via the QMP protocol. These tests allow you to ensure
that QEMU can be started up cleanly with your new device model added.
You can even trigger behaviour by sending a series of commands to the
backend. Usually there is only a minimal amount of guest code running
on the emulation itself.

Our avocado tests are more of a black box whole system test. Here a
QEMU instance is booted up with a full software stack (e.g. a
distribution kernel and userspace). A lot of tests just check the
combination successfully boots up although it is possible to trigger
additional steps after the fact. Generally we prefer to use upstream
distro kernels because it simplifies the hosting of artefacts but if
custom images are needed that can be done to. We deliberately avoid
hosting binary blobs in the QEMU infrastructure to avoid complications
with licensing requirements so please ensure there are instructions
for how to build them if needed.

Finally any new machine or device should come with some documentation
on how to enable and use it. QEMU's command line interface presents a
dazzlingly large array of options and features which often require
frontend and backend options to work together. If you want your
feature to be usable by other users your series should include an
addition to the fine manual describing some common configurations with
some example command lines. 

In conclusion
-------------

QEMU is a large multi-featured open source project with its fair share
of dusty corners and a large amount of folk knowledge spread between
over a hundred sub-system maintainers. While the project is keen to
incorporate new features doing so has implications for the long term
maintainability of the project. Incorporating those new features is
easier when the project can be assured that the feature is well
documented and easy to test for regressions. The ideal is for features
to come with an active and engaged maintainer who can address patches
and help get changes up-streamed in a timely manner. It's through the
efforts of it's maintainers that QEMU remains the active and useful
project that it is today.
