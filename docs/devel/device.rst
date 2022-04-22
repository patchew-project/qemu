QEMU device life-cycle
======================

This document details the specifics of devices.

Devices can be created in two ways: either internally by code or through a
user interface:

+ command line interface provides ``-device`` option
+ QAPI interface provides ``device_add`` command

Error handling is most important for the user interfaces. Internal code is
generally designed so that errors do not happen and if some happen, the error
is probably fatal (and QEMU will exit or abort).

Devices are a particular type of QEMU objects. In addition of the
``instance_init``, ``instance_post_init``, ``unparent`` and
``instance_finalize`` methods (common to all QOM objects), they have the
additional methods:

+ ``realize``
+ ``unrealize``

In the following we will ignore ``instance_post_init`` and consider is
associated with ``instance_init``.

``realize`` is the only method that can fail. In that case it should
return an adequate error. Some devices does not do this and should
not be created by means of user interfaces.

Device succesfully realized
---------------------------

The normal use case for device is the following:

1. ``instance_init``
2. ``realize`` with success
3. The device takes part in emulation
4. ``unrealize`` and ``unparent``
5. ``instance_finalize``

``instance_init`` and ``realize`` are part of the device creation procedure, whereas
``unrealize`` and ``instance_finalize`` are part of the device deletion procedure.

In case of an object created by code, ``realize`` has to be done explicitly
(eg: by calling ``qdev_realize(...)``). This is done automatically in case of a
device created via a user interface.

On the other hand ``unrealize`` is done automatically.
``unparent`` will take care of unrealizing the device then undoing any bus
relationships (children and parent).

Note that ``instance_finalize`` may not occur just after ``unrealize`` because
other objects than the parent can hold references to a device. It may even not
happen at all if a reference is never released.

Device realize failure
----------------------

This use case is most important when the device is created through a user
interface. The user might for example invalid properties and in that case
realize will fail and the device should then be deleted.

1. ``instance_init``
2. ``realize`` failure
3. ``unparent``
4. ``instance_finalize``

Failure to create a device should not leave traces. The emulation state after
that should be as if the device has not be created. ``realize`` and
``instance_finalize`` must take care of this.

Device help
-----------

Last use case is only a user interface case. When requesting help about a device
type, the following life cycle exists:

1. ``instance_init``
2. Introspect device properties
3. ``unparent``
4. ``instance_finalize``

This use case is simple but it means that ``instance_finalize`` cannot assume that
``realize`` has been called.

Implementation consequences
---------------------------

A device developer should ensure the above use cases are
supported so that the device is *user-creatable*.

In particular, fail cases must checked in realize and reported using the error
parameter. One should particularly take care of cleaning correctly whatever has
been previously done if realize fails. Cleaning tasks (eg: memory freeing) can
be done in ``realize`` or ``instance_finalize`` as they will be called in a row by
the user interface.

To this end ``realize`` must ensure than no additional reference to the device is
dangling when it fails. Otherwise the device will never be finalized and deleted.

If a device has created some children, they should be deleted as well in the
cleaning process. If ``object_initialize_child()`` was used to create a child
hosted into the device structure, the child memory space will disappear with the
parent. No reference to such child must be dangling to ensure the child will
not survive its parent deletion.

Although it is not asserted by code, one can assume ``realize`` will not be tried
again in case of failure and that the device will be finalized if no references
have been added during ``realize``.

