***************
Testing in QEMU
***************

QEMU is a large and complex project which can be configured in a
multitude of ways. As it's impossible for an individual developer to
manually test all of these we rely on a whole suite of automated
testing approaches to ensure regressions are kept to a minimum.

The following sections give a broad overview of the testing
infrastructure as well as some detailed introductions into more
advanced testing topics.

.. toctree::
   :maxdepth: 2

   testing
   fuzzing
   control-flow-integrity
   qtest
