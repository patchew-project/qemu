*************
QEMU Concepts
*************

There are a number of high level concepts that are useful to
understand when working with the code base. Perhaps the most pervasive
is the QEMU Object Model (QOM) which underpins much of the flexibility
and configurable of the project. The following sections document that
as well as diving into other concepts that are useful to know if
working on some areas of the code.

.. toctree::
   :maxdepth: 2

   qom
   clocks
   reset
   block-coroutine-wrapper
   migration
   multi-process
   s390-dasd-ipl
