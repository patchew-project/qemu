==========================
QAPI Golang code generator
==========================

..
   Copyright (C) 2023 Red Hat, Inc.

   This work is licensed under the terms of the GNU GPL, version 2 or
   later.  See the COPYING file in the top-level directory.


Introduction
============

This document provides information of how the generated Go code maps
with the QAPI specification, clarifying design decisions when needed.


Scope of the generated Go code
==============================

The scope is limited to data structures that can interpret and be used
to generate valid QMP messages. These data structures are generated
from a QAPI schema and should be able to handle QMP messages from the
same schema.

The generated Go code is a Go module with data structs that uses Go
standard library ``encoding/json``, implementing its field tags and
Marshal interface whenever needed.


QAPI types to Go structs
========================

Enum
----

Enums are mapped as strings in Go, using a specified string type per
Enum to help with type safety in the Go application.

::

    { 'enum': 'HostMemPolicy',
      'data': [ 'default', 'preferred', 'bind', 'interleave' ] }

.. code-block:: go

    type HostMemPolicy string

    const (
        HostMemPolicyDefault    HostMemPolicy = "default"
        HostMemPolicyPreferred  HostMemPolicy = "preferred"
        HostMemPolicyBind       HostMemPolicy = "bind"
        HostMemPolicyInterleave HostMemPolicy = "interleave"
    )


Struct
------

The mapping between a QAPI struct in Go struct is very straightforward.
 - Each member of the QAPI struct has its own field in a Go struct.
 - Optional members are pointers type with 'omitempty' field tag set

One important design decision was to _not_ embed base struct, copying
the base members to the original struct. This reduces the complexity
for the Go application.

::

    { 'struct': 'BlockExportOptionsNbdBase',
      'data': { '*name': 'str', '*description': 'str' } }

    { 'struct': 'BlockExportOptionsNbd',
      'base': 'BlockExportOptionsNbdBase',
      'data': { '*bitmaps': ['BlockDirtyBitmapOrStr'],
                '*allocation-depth': 'bool' } }

.. code-block:: go

    type BlockExportOptionsNbd struct {
        Name        *string `json:"name,omitempty"`
        Description *string `json:"description,omitempty"`

        Bitmaps         []BlockDirtyBitmapOrStr `json:"bitmaps,omitempty"`
        AllocationDepth *bool                   `json:"allocation-depth,omitempty"`
    }


Union
-----

Unions in QAPI are binded to a Enum type which provides all possible
branches of the union. The most important caveat here is that the Union
does not need to have a complex type implemented for all possible
branches of the Enum. Receiving a enum value of a unimplemented branch
is valid.

For this reason, the generated Go struct will define a field for each
Enum value. The Go type defined for unbranched Enum values is bool

Go struct and also implement the Marshal interface.

As each Union Go struct type has both the discriminator field and
optional fields, it is important to note that when converting Go struct
to JSON, we only consider the discriminator field if no optional field
member was set. In practice, the user should use the optional fields if
the QAPI Union type has defined them, otherwise the user can set the
discriminator field for the unbranched enum value.

::

    { 'union': 'ImageInfoSpecificQCow2Encryption',
      'base': 'ImageInfoSpecificQCow2EncryptionBase',
      'discriminator': 'format',
      'data': { 'luks': 'QCryptoBlockInfoLUKS' } }

.. code-block:: go

    type ImageInfoSpecificQCow2Encryption struct {
        // Variants fields
        Luks *QCryptoBlockInfoLUKS `json:"-"`
        // Unbranched enum fields
        Aes bool `json:"-"`
    }

    func (s ImageInfoSpecificQCow2Encryption) MarshalJSON() ([]byte, error) {
        // ...
        // Logic for branched Enum
        if s.Luks != nil && err == nil {
            if len(bytes) != 0 {
                err = errors.New(`multiple variant fields set`)
            } else if err = unwrapToMap(m, s.Luks); err == nil {
                m["format"] = BlockdevQcow2EncryptionFormatLuks
                bytes, err = json.Marshal(m)
            }
        }

        // Logic for unbranched Enum
        if s.Aes && err == nil {
            if len(bytes) != 0 {
                err = errors.New(`multiple variant fields set`)
            } else {
                m["format"] = BlockdevQcow2EncryptionFormatAes
                bytes, err = json.Marshal(m)
            }
        }

        // ...
        // Handle errors
    }


    func (s *ImageInfoSpecificQCow2Encryption) UnmarshalJSON(data []byte) error {
        // ...

        switch tmp.Format {
        case BlockdevQcow2EncryptionFormatLuks:
            s.Luks = new(QCryptoBlockInfoLUKS)
            if err := json.Unmarshal(data, s.Luks); err != nil {
                s.Luks = nil
                return err
            }
        case BlockdevQcow2EncryptionFormatAes:
            s.Aes = true

        default:
            return fmt.Errorf("error: unmarshal: ImageInfoSpecificQCow2Encryption: received unrecognized value: '%s'",
                tmp.Format)
        }
        return nil
    }


Alternate
---------

Like Unions, alternates can have a few branches. Unlike Unions, they
don't have a discriminator field and each branch should be a different
class of Type entirely (e.g: You can't have two branches of type int in
one Alternate).

While the marshalling is similar to Unions, the unmarshalling uses a
try-and-error approach, trying to fit the data payload in one of the
Alternate fields.

The biggest caveat is handling Alternates that can take JSON Null as
value. The issue lies on ``encoding/json`` library limitation where
unmarshalling JSON Null data to a Go struct which has the 'omitempty'
field that, it bypass the Marshal interface. The same happens when
marshalling, if the field tag 'omitempty' is used, a nil pointer would
never be translated to null JSON value.

The problem being, we use pointer to type plus ``omitempty`` field to
express a QAPI optional member.

In order to handle JSON Null, the generator needs to do the following:
  - Read the QAPI schema prior to generate any code and cache
    all alternate types that can take JSON Null
  - For all Go structs that should be considered optional and they type
    are one of those alternates, do not set ``omitempty`` and implement
    Marshal interface for this Go struct, to properly handle JSON Null
  - In the Alternate, uses a boolean 'IsNull' to express a JSON Null
    and implement the AbsentAlternate interface, to help sturcts know
    if a given Alternate type should be considered Absent (not set) or
    any other possible Value, including JSON Null.

::

    { 'alternate': 'BlockdevRefOrNull',
      'data': { 'definition': 'BlockdevOptions',
                'reference': 'str',
                'null': 'null' } }

.. code-block:: go

    type BlockdevRefOrNull struct {
        Definition *BlockdevOptions
        Reference  *string
        IsNull     bool
    }

    func (s *BlockdevRefOrNull) ToAnyOrAbsent() (any, bool) {
        if s != nil {
            if s.IsNull {
                return nil, false
            } else if s.Definition != nil {
                return *s.Definition, false
            } else if s.Reference != nil {
                return *s.Reference, false
            }
        }

        return nil, true
    }

    func (s BlockdevRefOrNull) MarshalJSON() ([]byte, error) {
        if s.IsNull {
            return []byte("null"), nil
        } else if s.Definition != nil {
            return json.Marshal(s.Definition)
        } else if s.Reference != nil {
            return json.Marshal(s.Reference)
        }
        return []byte("{}"), nil
    }

    func (s *BlockdevRefOrNull) UnmarshalJSON(data []byte) error {
        // Check for json-null first
        if string(data) == "null" {
            s.IsNull = true
            return nil
        }
        // Check for BlockdevOptions
        {
            s.Definition = new(BlockdevOptions)
            if err := StrictDecode(s.Definition, data); err == nil {
                return nil
            }
            s.Definition = nil
        }
        // Check for string
        {
            s.Reference = new(string)
            if err := StrictDecode(s.Reference, data); err == nil {
                return nil
            }
            s.Reference = nil
        }

        return fmt.Errorf("Can't convert to BlockdevRefOrNull: %s", string(data))
    }


Event
-----

All events are mapped to its own struct with the additional
MessageTimestamp field, for the over-the-wire 'timestamp' value.

Marshaling and Unmarshaling happens over the Event interface, so users
should use the MarshalEvent() and UnmarshalEvent() methods.

::

    { 'event': 'SHUTDOWN',
      'data': { 'guest': 'bool',
                'reason': 'ShutdownCause' } }

.. code-block:: go

    type Event interface {
        GetName() string
        GetTimestamp() Timestamp
    }

    type ShutdownEvent struct {
        MessageTimestamp Timestamp     `json:"-"`
        Guest            bool          `json:"guest"`
        Reason           ShutdownCause `json:"reason"`
    }

    func (s *ShutdownEvent) GetName() string {
        return "SHUTDOWN"
    }

    func (s *ShutdownEvent) GetTimestamp() Timestamp {
        return s.MessageTimestamp
    }


Command
-------

All commands are mapped to its own struct with the additional MessageId
field for the optional 'id'. If the command has a boxed data struct,
the option struct will be embed in the command struct.

As commands do require a return value, every command has its own return
type. The Command interface has a GetReturnType() method that returns a
CommandReturn interface, to help Go application handling the data.

Marshaling and Unmarshaling happens over the Command interface, so
users should use the MarshalCommand() and UnmarshalCommand() methods.

::

   { 'command': 'set_password',
     'boxed': true,
     'data': 'SetPasswordOptions' }

.. code-block:: go

    type Command interface {
        GetId() string
        GetName() string
        GetReturnType() CommandReturn
    }

    // SetPasswordOptions is embed
    type SetPasswordCommand struct {
        SetPasswordOptions
        MessageId string `json:"-"`
    }

    // This is an union
    type SetPasswordOptions struct {
        Protocol  DisplayProtocol    `json:"protocol"`
        Password  string             `json:"password"`
        Connected *SetPasswordAction `json:"connected,omitempty"`

        // Variants fields
        Vnc *SetPasswordOptionsVnc `json:"-"`
    }

Now an example of a command without boxed type.

::

    { 'command': 'set_link',
      'data': {'name': 'str', 'up': 'bool'} }

.. code-block:: go

    type SetLinkCommand struct {
        MessageId string `json:"-"`
        Name      string `json:"name"`
        Up        bool   `json:"up"`
    }

Known issues
============

- Type names might not follow proper Go convention. Andrea suggested an
  annotation to the QAPI schema that could solve it.
  https://lists.gnu.org/archive/html/qemu-devel/2022-05/msg00127.html
