Independent Guest Virtual Machine (IGVM) support
================================================

IGVM files are designed to encaspulate all the information required to launch a
virtual machine on any given virtualization stack in a deterministic way. This
allows the cryptographic measurement of initial guest state for Confidential
Guests to be calculated when the IGVM file is built, allowing a relying party to
verify the initial state of a guest via a remote attestation.

QEMU supports IGVM files through the Confidential Guest Support object. An igvm
filename can optionally be passed to the object which will subsequently be
parsed and used to configure the guest state prior to launching the guest.

Further Information on IGVM
---------------------------

Information about the IGVM format, including links to the format specification
and documentation for the Rust and C libraries can be found at the project
repository:

https://github.com/microsoft/igvm


Supported Confidential Guests
-----------------------------

Currently, IGVM files can be provided for Confidential Guests on host systems
that support AMD SEV and SEV-ES.

IGVM files contain a set of directives. Not every directive is supported by
every Confidential Guest type. For example, setting the initial CPU state is not
supported on AMD SEV due to the platform not supporting encrypted save state
regions. However, this is supported on SEV-ES.

When an IGVM file contains directives that are not supported for the active
platform, an error is displayed and the guest launch is aborted.

Firmware Images with IGVM
-------------------------

When an IGVM filename is specified for a Confidential Guest Support object it
overrides the default handling of system firmware: the firmware image, such as
an OVMF binary should be contained as a payload of the IGVM file and not
provided as a flash drive. The default QEMU firmware is not automatically mapped
into guest memory.

Running a Confidential Guest configured using IGVM
--------------------------------------------------

To run a confidential guest configured with IGVM you need to add the
``igvm-file`` parameter to the "confidential guest support" object:

Example (for AMD SEV)::

    qemu-system-x86_64 \
        <other parameters> \
        -machine ...,confidential-guest-support=sev0 \
        -object sev-guest,id=sev0,cbitpos=47,reduced-phys-bits=1,igvm-file=/path/to/guest.igvm
