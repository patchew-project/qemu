============
DBus VMState
============

Introduction
============

Helper processes may have their state migrated with the help of
QEMU "dbus-vmstate" objects.

At this point, the connection to the helper is done in DBus
peer-to-peer mode (no initial Hello, and no bus name for
communication). The helper must be listening to the given address.

Helper may save arbitrary data to be transferred in the migration
stream and restored/loaded on destination.

The data amount to be transferred is limited to 1Mb. The state must be
saved quickly (a few seconds maximum). (DBus imposes a time limit on
reply anyway, and migration would fail if the data isn't given quickly
enough)

Interface
=========

On /org/qemu/VMState1 object path:

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

A utf8 encoded string that identifies the helper uniquely.
Must be <256 bytes.

Load(in u8[] bytes) method
--------------------------

The method called on destination with the state to restore.

The helper may be initially started in a waiting state (with
an --incoming argument for example), and it may resume on load
success.

An error may be returned to the caller.

Save(out u8[] bytes) method
---------------------------

The method called on the source to get the current state to be
migrated. The helper should continue to run normally.

An error may be returned to the caller.
