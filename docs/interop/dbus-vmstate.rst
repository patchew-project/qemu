============
DBus VMState
============

Introduction
============

Helper processes may have their state migrated with the help of the
QEMU object "dbus-vmstate".

Upon migration, QEMU will go through the queue of "org.qemu.VMState1"
DBus name owners and query their "Id". It must be unique among the
helpers.

It will then save arbitrary data of each Id to be transferred in the
migration stream and restored/loaded at the corresponding destination
helper.

The data amount to be transferred is limited to 1Mb. The state must be
saved quickly (a few seconds maximum). (DBus imposes a time limit on
reply anyway, and migration would fail if data isn't given quickly
enough)

dbus-vmstate object can be configured with the expected list of
helpers by setting its "id-list" property, with a coma-separated Id
list.

Interface
=========

.. code:: xml

  <interface name="org.qemu.VMState1">
    <property name="Id" type="s" access="read"/>
    <method name="Load">
      <arg type="ay" name="data" direction="in"/>
    </method>
    <method name="Save">
      <arg type="ay" name="data" direction="out"/>
    </method>
  </interface>

"Id" property
-------------

A string that identifies the helper uniquely.

Load(in u8[] bytes) method
--------------------------

The method called on destination with the state to restore.

The helper may be initially started in a waiting state (with
an --incoming argument for example), and it may resume on success.

An error may be returned to the caller.

Save(out u8[] bytes) method
---------------------------

The method called on the source to get the current state to be
migrated. The helper should continue to run normally.

An error may be returned to the caller.
