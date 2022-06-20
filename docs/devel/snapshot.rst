================
Snapshot/restore
================

The ability to rapidly snapshot and restore guest VM state is a
crucial component of fuzzing applications with QEMU. A special virtual
device can be used by fuzzers to interface with snapshot/restores
commands in QEMU. The virtual device should have the following
commands supported that can be called by the guest:

- snapshot: save a copy of the guest VM memory, registers, and virtual
  device state
- restore: restore the saved copy of guest VM state
- coverage_location: given a location in guest memory, specifying
  where the coverage data is to be passed to the fuzzer
- input_location: specify where in the guest memory the fuzzing input
  should be stored
- done: indicates whether or not the run succeeded and that the
  coverage data has been populated

The first version of the virtual device will only accept snapshot and
restore commands from the guest. Coverage data will be collected by
code on the guest with source-based coverage tracking.

Further expansions could include controlling the snapshot/restore from
host and gathering code coverage information directly from TCG.
