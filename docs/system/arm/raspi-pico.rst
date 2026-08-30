.. SPDX-License-Identifier: GPL-2.0-or-later

Raspberry Pi Pico board (``raspi-pico``)
========================================

The ``raspi-pico`` machine models a minimal Raspberry Pi Pico 1 board based
on the RP2040 microcontroller.  The current model is intended for bare-metal
bring-up and tests that execute code from the RP2040 external flash XIP
window.

RFC lineage
-----------

This machine starts from Alex Bennee's 2022 RP2040/Pico RFC patch series.
The RP2040 SoC skeleton, Pico machine, and memory map are adapted from that
series to the current QEMU tree.  The RFC ``pc-bios/pipico.rom`` image was
used for bring-up experiments, but it is not installed as a QEMU firmware
blob by this machine.

RFC patch 0005's mask ROM loading logic is adapted for the current machine:
an external boot ROM can be supplied explicitly with ``-bios``.  QEMU still
uses a small synthetic boot ROM for direct XIP bring-up, so existing firmware
tests remain stable without requiring an external firmware image.

Supported devices
-----------------

 * Two Cortex-M0+ cores. With QEMU's synthetic ROM, core 1 starts in ROM and
   waits for the SDK FIFO launch sequence. With an external ROM image, the
   modeled ``PSM.FRCE_OFF.PROC1`` path can release core 1 at the ROM reset
   vector, but the external-ROM multicore flow remains a bring-up path.
 * Cortex-M0+ MPU with eight protection regions
 * 16 KiB boot ROM window
 * 264 KiB SRAM
 * 2 MiB external flash contents mapped through the XIP window and XIP alias
   windows
 * XIP/SSI flash command path for read, page program, sector erase, unique ID,
   XIP stream and SDK-style SSI RX DMA reads
 * Clock generator, crystal oscillator, ring oscillator, PLL_SYS and PLL_USB
   programmer-visible registers
 * Reset controller, PSM, watchdog, timer, SYSINFO, SYSCFG, TBMAN and
   VREG_AND_CHIP_RESET programmer-visible registers
 * BUSCTRL priority registers and shallow performance counters
 * SIO core ID, inter-core FIFO, spinlocks, divider and interpolators
 * DMA engine with memory, UART, XIP stream, XIP/SSI RX and timer DREQ support
 * IO_BANK0, IO_QSPI, PADS_BANK0 and PADS_QSPI shallow pin-control models
 * UART0 and UART1 through QEMU serial backends
 * USB DPRAM and shallow USB controller register storage

Unsupported or shallow devices
------------------------------

This machine is not a complete RP2040 model.  The following blocks are not
implemented, or are only present as shallow register storage:

 * PIO state machines, instruction execution, FIFOs, IRQs and DMA pacing are
   not implemented.
 * USB device and host transactions, endpoint state machines and BOOTSEL
   mass-storage programming are not implemented.  USB DPRAM and the basic
   controller register window exist for firmware probing only.
 * SPI0, SPI1, I2C0, I2C1, PWM, ADC and the temperature sensor are not
   implemented.
 * BUSCTRL does not model arbitration.  Its performance counters are
   deterministic programmer-visible counters, not measurements of actual bus
   events.
 * The RTC block is not implemented.
 * General GPIO electrical behaviour, pad-level input sampling, edge
   detection, external pin wiring and alternate UART pin mappings are not
   implemented.  IO_BANK0 is sufficient for function-select storage and for
   gating the supported UART0/UART1 pins.
 * The XIP cache is not modeled.  XIP aliases are functional views of the same
   flash storage, but cache hit/miss counters and cache allocation behaviour
   are not timing-accurate.
 * QEMU's normal gdbstub can debug the emulated Cortex-M0+ CPUs.  The RP2040
   SWD/debug fabric itself is not implemented: debug pause inputs and
   ``DBGFORCE``-style registers are stored where useful, but they do not model
   an external SWD probe connected to the chip.
 * DMA is implemented for the paths listed above, but DREQ sources belonging
   to unimplemented peripherals do not transfer data.
 * The synthetic ROM provides a useful subset of the RP2040 function and data
   tables, including memory and bit helpers, floating-point helpers and
   accelerated flash operations.  Missing table entries report an explicit
   not-implemented diagnostic.  The external mask ROM path is available
   through ``-bios`` for user-provided RP2040 ROM images such as
   ``pipico.rom``; QEMU does not ship that ROM image, and USB BOOTSEL
   mass-storage mode is not implemented.

Boot options
------------

For direct bring-up, a firmware image can be loaded into the XIP window with
``-kernel``.  ELF images linked at ``0x10000000`` and Pico 1 UF2 images are
accepted, with raw images loaded at ``0x10000000`` as a fallback:

.. code-block:: bash

  $ qemu-system-arm -machine raspi-pico -kernel firmware.elf -serial stdio

The same option accepts the UF2 image normally copied to the Pico's BOOTSEL
USB mass-storage device:

.. code-block:: bash

  $ qemu-system-arm -machine raspi-pico -kernel firmware.uf2 -serial stdio

QEMU does not emulate the Pico BOOTSEL USB mass-storage programming mode.  A
UF2 file is loaded directly by ``-kernel``; guest firmware cannot receive a
UF2 through an emulated USB drive.

The machine also accepts a raw initial flash image:

.. code-block:: bash

  $ qemu-system-arm -machine raspi-pico,flash-file=flash.bin -serial stdio

Bytes not provided by the raw flash image are initialized to the NOR erased
state, ``0xff``.

If both ``flash-file`` and ``-kernel`` are specified, the raw flash file is
loaded first, then the ``-kernel`` image is overlaid into the emulated XIP
flash.  Only the flash ranges covered by the ELF, UF2 or raw image are
replaced; the rest of the existing raw flash contents are preserved.  The
complete emulated flash image is written back to the raw file, so a later run
with only ``flash-file`` restarts from the overlaid image.
Successful guest sector erase and page program commands are also written back
to the raw file.

The Pico SDK uses the external SPI NOR flash unique ID as the Pico 1 board
identifier.  The emulated flash reports the stable default ID
``3eb8a7493fcc0608``.  Tests that need a different board identity can override
it with:

.. code-block:: bash

  $ qemu-system-arm -machine raspi-pico,flash-uid=0011223344556677 \
      -kernel firmware.elf -serial stdio

The flash UID is not stored in, nor appended to, ``flash-file``; that file
remains the raw bytes of the guest-addressable flash array.

The ring oscillator ``RANDOMBIT`` stream is backed by QEMU's guest-visible
random source by default.  For reproducible tests, a deterministic stream can
be requested with:

.. code-block:: bash

  $ qemu-system-arm -machine raspi-pico,rosc-random-seed=0x1234 \
      -kernel firmware.elf -serial stdio

Debugging with GDB uses QEMU's normal gdbstub.  For example:

.. code-block:: bash

  $ qemu-system-arm -machine raspi-pico -kernel firmware.elf \
      -serial stdio -S -gdb tcp::1234

Then connect an ARM embedded GDB:

.. code-block:: bash

  (gdb) target remote :1234

This debugs the emulated Cortex-M0+ CPU through QEMU.  It does not emulate
the RP2040 SWD debug port or an external SWD probe.

By default, guest ``BKPT`` instructions retain their architectural Cortex-M
behaviour.  In particular, ``BKPT #0`` used by the Pico SDK ``_exit()`` path
is delivered through the normal DebugMonitor/HardFault rules; merely running
under QEMU does not turn it into a process exit.

Automated tests that deliberately use the Pico SDK exit convention can opt
in to a machine-local compatibility path:

.. code-block:: bash

  $ qemu-system-arm -machine raspi-pico,pico-sdk-exit=on \
      -kernel firmware.elf -serial stdio

When enabled, QEMU overlays the initial application HardFault vector at
``0x1000010c`` with a trampoline.  A HardFault caused by ``BKPT #0`` requests
guest shutdown using the stacked ``r0`` value as the exit status.  Other
HardFaults are forwarded to the application's original handler.  This option
is disabled by default, requires a valid Thumb HardFault vector in the loaded
XIP image, and does not modify ``flash-file``.  It is a Pico SDK test
convenience rather than an RP2040 hardware feature.  Standard Arm semihosting
remains available independently with ``-semihosting-config``.

Pico UF2 images can be converted to this raw flash format with:

.. code-block:: bash

  $ scripts/uf2-to-flash.py firmware.uf2 flash.bin

An RP2040 boot ROM image can be supplied explicitly with ``-bios``:

.. code-block:: bash

  $ qemu-system-arm -machine raspi-pico -bios pipico.rom -serial stdio

The file name is resolved through QEMU's BIOS search path, the same mechanism
used by other machines for firmware blobs.  QEMU does not ship an RP2040 boot
ROM image for this machine; use ``-bios`` with a user-provided ROM image when
testing the external mask-ROM path.  Direct XIP bring-up keeps using the
synthetic boot ROM described above, and explicit ``-bios`` still overrides the
synthetic ROM.

Mask ROM bring-up tracing
-------------------------

The real mask ROM path can be explored with QEMU's unimplemented-device log:

.. code-block:: bash

  $ qemu-system-arm -machine raspi-pico,flash-file=flash.bin \
      -bios pipico.rom -display none -serial none \
      -d unimp,guest_errors -D rp2040-bios-mmio.log

The RP2040 model names unimplemented MMIO blocks in the log and includes the
absolute address, register offset, access size and write value.  The XIP/SSI
register block also logs APB register accesses through the same ``unimp`` log
mask, without logging every normal XIP instruction fetch.

Most early boot polls that were useful during bring-up are now handled by
the corresponding shallow device models: clocks, XOSC, PLLs, reset
controller, watchdog scratch registers, XIP control aliases, SYSINFO,
SYSCFG, VREG_AND_CHIP_RESET, TBMAN, PSM, SIO and the QSPI IO path.  New
``LOG_UNIMP`` messages from this command are therefore a
signal that the external mask-ROM path has reached a peripheral or register
that is still outside the modeled subset.

The external ``-bios`` path is intended for hardware-compatibility bring-up
and for comparing the synthetic ROM against the real RP2040 boot ROM flow.
It is more sensitive to missing low-level hardware details than the synthetic
ROM path, and QEMU does not ship the ``pipico.rom`` image.

The in-tree qtests and ``check-tcg`` programs keep coverage self-contained by
reproducing important hardware and SDK sequences without depending on the
Pico SDK or on a user-provided mask ROM image.

Clock and XOSC model
--------------------

The RP2040 datasheet describes clock generator ``SELECTED`` registers as
one-hot status registers for glitchless muxes, and notes that software should
poll them until a source switch completes.  See datasheet pages 203 to 216.
The current QEMU model returns stable one-hot selected values immediately and
updates QEMU ``Clock`` outputs for ``clk-ref``, ``clk-sys``, ``clk-peri``,
``clk-usb``, ``clk-adc`` and ``clk-rtc``.  It models frequencies and software
visible register state, not analog transition latency.

The crystal oscillator model follows the XOSC programmer-visible behaviour
described by the datasheet: XOSC starts disabled, firmware writes the enable
code to ``CTRL``, and then polls ``STATUS.STABLE`` until the oscillator is
usable.  See datasheet pages 217 to 220.  QEMU asserts ``STABLE``
immediately once XOSC is enabled and awake, keeps ``BADWRITE`` sticky until
cleared, and implements the documented ``STARTUP``, ``DORMANT`` and
``COUNT`` registers at the level needed by early boot.

The ring oscillator model follows the programmer-visible register layout
described by the datasheet: ``CTRL``, ``FREQA``, ``FREQB``, ``DORMANT``,
``DIV``, ``PHASE``, ``STATUS``, ``RANDOMBIT`` and ``COUNT``.  See datasheet
pages 221 to 227.  QEMU models a stable nominal ROSC and updates a QEMU
``Clock`` output from the visible enable/dormant/divider state.  ``COUNT`` is
derived from QEMU virtual time rather than CPU cycles; in normal execution
this follows elapsed host time, while in ``icount`` mode it follows QEMU's
deterministic virtual clock.  ``RANDOMBIT`` is not an analog oscillator model:
without ``rosc-random-seed`` it refills from QEMU's guest-visible random
source, and with ``rosc-random-seed`` it uses a deterministic pseudo-random
stream for repeatable tests.  The model does not emulate analog frequency
variation with process, voltage or temperature.

The QSPI IO bank model implements the documented IO_QSPI register layout for
the six QSPI pins.  It stores each pin's ``CTRL`` register, returns stable
zero ``STATUS`` values, implements shallow interrupt enable/force/status
registers, supports the RP2040 atomic aliases, and forwards forced
``GPIO_QSPI_SS`` output changes to the XIP/SSI flash model.  This is enough
for boot firmware to configure the QSPI pin muxing and to bracket serial flash
commands around the XIP/SSI controller.  It does not emulate pad electrical
behaviour or serial flash transfers; those belong to the pad and XIP/SSI
models.

Reset controller model
----------------------

The RP2040 datasheet describes the reset controller at ``0x4000c000`` with
``RESET``, ``WDSEL`` and ``RESET_DONE`` registers.  ``RESET`` holds a
peripheral in reset while its bit is set, and ``RESET_DONE`` reports that the
peripheral's registers are ready once reset is deasserted.  See datasheet
pages 175 to 177.

The current QEMU model stores ``RESET`` and ``WDSEL`` for documented bits
0..24, implements the RP2040 atomic alias windows, and derives
``RESET_DONE`` immediately as the inverse of ``RESET`` for those bits.  It
does not yet propagate resets into the individual peripheral models or model
reset completion delays.

VREG and chip reset model
-------------------------

The RP2040 datasheet describes the shared ``VREG_AND_CHIP_RESET`` register
window at ``0x40064000`` with ``VREG``, ``BOD`` and ``CHIP_RESET`` registers.
``CHIP_RESET`` records chip-level reset sources: power-on/brown-out, RUN pin,
and Rescue Debug Port.  See datasheet pages 157 to 158 and 167.

The current QEMU model stores the writable ``VREG`` and ``BOD`` fields,
reports ``VREG.ROK`` as stable when the regulator is enabled and not in high
impedance mode, and stores the software-visible Rescue Debug Port flag in
``CHIP_RESET``.  Watchdog reset cause is reported by the watchdog block's
``REASON`` register; it is not reflected in ``CHIP_RESET`` because the
documented ``CHIP_RESET`` source fields do not include watchdog reset.

TBMAN model
-----------

The RP2040 datasheet describes ``TBMAN`` as a testbench manager used during
chip development simulations.  On real hardware it only exposes a
``PLATFORM`` register indicating that the platform is ASIC; this is duplicated
by ``SYSINFO.PLATFORM``.  See datasheet pages 309 to 310.

The current QEMU model implements this real-chip subset and returns
``TBMAN.PLATFORM.ASIC`` set.  It deliberately does not expose testbench
simulation controls, because those controls would imply a simulation
environment outside the RP2040 SoC model.

PSM model
---------

The RP2040 datasheet describes the power-on state machine at ``0x40010000``
with ``FRCE_ON``, ``FRCE_OFF``, ``WDSEL`` and ``DONE`` registers.  The Pico
SDK uses ``FRCE_OFF.PROC1`` in ``multicore_reset_core1()`` to hold core 1 off
and then release it before the ROM FIFO launch protocol.  See datasheet pages
179 to 182.

The current QEMU model stores the documented bits 0..16, implements the
RP2040 atomic alias windows, and derives ``DONE`` immediately as the inverse
of ``FRCE_OFF``.  On reset, ``FRCE_OFF`` is clear.  Setting
``FRCE_OFF.PROC1`` powers off proc1 in the QEMU model; clearing it powers
proc1 back on at the ROM reset vector.

PLL model
---------

The RP2040 datasheet describes ``PLL_SYS`` and ``PLL_USB`` at ``0x40028000``
and ``0x4002c000``.  Each PLL exposes ``CS``, ``PWR``, ``FBDIV_INT`` and
``PRIM`` registers; firmware powers the PLL, waits for ``CS.LOCK``, and then
enables the post dividers.  The documented output frequency is
``(FREF / REFDIV) * FBDIV / (POSTDIV1 * POSTDIV2)``.  See datasheet pages
228 to 233.

The current QEMU model is intentionally shallow.  It stores the visible
registers, implements atomic alias writes, reports ``CS.LOCK`` immediately
when the PLL core is powered, and publishes a calculated QEMU ``Clock``
output.  This does not change instruction execution speed directly.  In QEMU,
``Clock`` objects describe the modeled hardware clock tree; TCG execution
rate is not a cycle-accurate function of the guest PLL.  The RP2040 clock
generator model still uses fixed PLL_SYS/PLL_USB frequencies, so dynamic PLL
output wiring is left for a later fidelity step.

RP2040 flash and XIP model
--------------------------

The RP2040 datasheet describes a 16 KiB mask ROM at ``0x00000000``, 264 KiB
of SRAM starting at ``0x20000000``, and external flash accessed through the
QSPI execute-in-place hardware.  See the RP2040 datasheet pages 120 to 122.

The flash is not directly attached as an ordinary parallel memory.  System
bus reads to the 16 MiB XIP window starting at ``0x10000000`` are translated
by the XIP hardware into external serial flash transfers.  The XIP block also
contains a 16 KiB cache and several aliases with different cache behaviour.
See datasheet pages 122 to 124.  The current QEMU model implements the main
XIP window and the ``NOALLOC``, ``NOCACHE`` and ``NOCACHE_NOALLOC`` aliases as
functional views of the same flash storage, while leaving cache timing for
later work.

The RP2040 XIP path is backed by the SSI controller.  The datasheet describes
the SSI as a Synopsys DW_apb_ssi controller connected to the QSPI pins and
forming part of the XIP block.  It can be configured to issue common serial
flash read sequences, including the standard ``0x03`` read command with a
24-bit address and the continuation-read path used by the Pico SDK
``flash/ssi_dma`` example.  The SSI DMA registers are modeled sufficiently for
RX DMA pacing from ``SSI_DR0`` through ``DREQ_XIP_SSIRX``.  See datasheet
pages 567 to 569.

The synthetic boot ROM provides the minimum startup path required by this
machine.  Core 0 copies the 256-byte boot2 image from XIP into SRAM and runs
it there.  Core 1 waits in ROM for the SDK-compatible FIFO launch sequence,
echoes each word, installs the requested ``VTOR`` and stack pointer, and
branches to the requested entry point.  Higher-level multicore lockout and
flash protocols remain firmware responsibilities.

The synthetic ROM also publishes RP2040-compatible lookup, function and data
tables.  It implements ``memcpy``, ``memset``, their word-oriented variants,
count/reverse helpers, and the floating-point and double-precision entries
covered by the in-tree tests.  These routines call a private QEMU service
region to provide the same functional result more efficiently; they do not
model ROM instruction timing.

The synthetic flash table entries similarly accelerate connect, XIP mode,
cache-flush, erase and program operations through the QEMU flash model.  Page
program and sector erase still enforce the modeled NOR rules.  These helpers
bypass the guest-visible SSI command sequence and complete atomically from the
CPUs' point of view, so they belong to the synthetic-ROM execution profile,
not to the RP2040 hardware model.  Firmware tested with an external
``pipico.rom`` instead exercises the modeled SIO lockout and SSI/XIP paths.
Unsupported ROM lookups, including ``reset_usb_boot``, retain an explicit
not-implemented diagnostic.

The external ``pipico.rom`` mask ROM path executes a user-provided RP2040
boot ROM image through ``-bios``.  It is useful when validating behaviour
that depends on real ROM hardware interactions, provided those interactions
stay within the modeled peripheral subset.  It is not a USB BOOTSEL
emulation path.

Firmware intended to terminate QEMU in a test can use standard Arm
semihosting ``SYS_EXIT`` or ``SYS_EXIT_EXTENDED`` with ``bkpt #0xab`` when
semihosting is enabled.  Ordinary ``bkpt`` instructions otherwise retain
their architectural Cortex-M exception behaviour unless the explicit
``pico-sdk-exit`` compatibility option described above is enabled.

The lower-level SSI/XIP command path remains responsible for modelling serial
flash command state and for raising the documented QEMU HardFault policy when
guest code executes from XIP while the flash model is busy.  Tests for that
busy/fault behaviour target the SSI/XIP model directly, or the ``pipico.rom``
path once the relevant ROM/hardware interaction is supported, rather than the
synthetic ROM's atomic helper shortcuts.

For software-driven flash operations, firmware programs the SSI through its
APB register interface at ``XIP_SSI_BASE``.  The important registers for the
initial emulation are ``CTRLR0``, ``CTRLR1``, ``SSIENR``, ``SER``, ``BAUDR``,
``SR`` and the data register window beginning at ``DR0``.  The data register
window feeds the transmit FIFO on writes and pops the receive FIFO on reads.
See datasheet pages 597 to 602.

Flash programming policy
------------------------

The emulation models guest-visible flash programming through the RP2040
XIP/SSI path rather than as a board-private back door.  The implemented
command set is:

 * ``0x01`` write status
 * ``0x06`` write enable
 * ``0x05`` read status
 * ``0x35`` read status register 2
 * ``0x03`` read
 * ``0x4b`` read unique ID
 * ``0xeb`` quad I/O read
 * ``0xa0`` continuation read
 * ``0x02`` page program
 * ``0x20`` sector erase

The flash contents follow NOR semantics:

 * the erased state is ``0xff``;
 * programming can only clear bits, equivalent to ``old & new``;
 * page program is limited to 256-byte pages;
 * sector erase operates on 4096-byte sectors.

Unsupported flash commands are currently ignored.  Out-of-range page program
and sector erase commands have no effect.  If such a command consumed write
enable state, the emulation clears write enable and does not enter the busy
state.

The datasheet notes that software must consider XIP cache coherence around
flash programming operations, and describes ROM routines that reconfigure the
SSI for erase/program flows before restoring a slow XIP read configuration.
It also notes that, between parts of that call sequence, the SSI is not in a
state where it can handle XIP accesses.  See datasheet pages 122 to 124 and
the boot ROM flash routine discussion on pages 134 to 135.

QEMU therefore uses the following deterministic policy: while an emulated
flash page program or sector erase is in progress, any access through the XIP
memory window to the same flash produces a bus error.  On the Cortex-M0+,
unsuitable instruction fetches or faulting memory accesses are reported via
HardFault; the RP2040 datasheet describes the Cortex-M0+ default memory map
and HardFault behaviour on pages 71 to 72.  This is an emulation policy chosen
to make incorrect execute-from-XIP-while-programming behaviour visible and
testable.  It is not intended to model precise flash timing.

UART models
-----------

The RP2040 datasheet states that each UART instance is based on ARM PrimeCell
UART PL011 revision r1p5, with 32-byte transmit and receive FIFOs.  It also
states that PL011 modem mode and IrDA mode are not supported by RP2040.  See
datasheet pages 417 to 419.

The current QEMU model therefore wires UART0 at ``0x40034000`` and UART1 at
``0x40038000`` to QEMU's existing PL011 device.  The register list and flag
register layout match the RP2040 UART programmer's model: ``UARTDR`` is at
offset ``0x000``, ``UARTRSR/UARTECR`` at ``0x004`` and ``UARTFR`` at
``0x018``.  See datasheet pages 429 to 431.

The RP2040 APB atomic alias windows for both UARTs are also mapped, because
the Pico SDK uses them when configuring UART registers.  ``UARTDMACR`` drives
UART0 and UART1 TX/RX DREQ lines into the RP2040 DMA model.

The console path uses QEMU's standard serial backends, so the host side can
still be selected with the usual ``-serial`` or ``-chardev`` options.  UART0
uses the first serial backend and UART1 uses the second one, for example:

.. code-block:: bash

  $ qemu-system-arm -machine raspi-pico -kernel firmware.elf \
      -serial stdio -serial tcp:127.0.0.1:1234,server,nowait

By default, the Pico machine requires the guest to route UARTs through
``IO_BANK0`` first: GPIO0/GPIO1 must have ``FUNCSEL=UART`` before UART0 host
serial transmit/receive is connected, and GPIO4/GPIO5 do the same for UART1.
This catches firmware that writes UART registers but forgets the Pico GPIO
function select.

For compatibility with very small bring-up payloads, this check can be
disabled with ``-machine raspi-pico,strict-uart-pins=off``.  ``PADS_BANK0``
stores the documented pad-control registers separately from this UART path.

For the initial console use case, the documented stable status behaviour is:

 * ``UARTFR.TXFE`` and ``UARTFR.RXFE`` follow QEMU PL011 FIFO state.
 * ``UARTFR.TXFF`` and ``UARTFR.RXFF`` follow QEMU PL011 FIFO fullness.
 * ``UARTFR.BUSY`` is not modeled with RP2040 transmission timing.
 * ``UARTFR.RI``, ``UARTFR.DCD`` and ``UARTFR.DSR`` are treated as absent
   modem-status inputs and remain deasserted.
 * ``UARTFR.CTS`` has no GPIO-backed CTS input yet and remains deasserted
   unless a future RP2040 UART shim connects it to the GPIO model.

This is sufficient for polling transmit firmware that waits for ``TXFF`` to
clear before writing ``UARTDR``.  A dedicated RP2040 UART wrapper can be added
later if firmware needs GPIO-backed CTS/RTS flow control, precise ``BUSY``
timing, or stricter masking of unsupported PL011 modem/IrDA features.

Known limitations
-----------------

 * Core 0 runs normally.  Core 1 starts in the synthetic ROM, echoes the
   SDK-compatible FIFO launch sequence, installs the provided ``VTOR``/stack,
   and branches to the provided entry point.  With an external mask ROM,
   ``PSM.FRCE_OFF.PROC1`` can release core 1 at the ROM reset vector, but the
   external-ROM core1 boot flow is not yet covered by an in-tree regression.
 * UART0 and UART1 currently use QEMU's PL011 model with the RP2040
   compatibility policy documented above.  The strict pin check currently
   covers the Pico GPIO0/GPIO1 UART0 path and GPIO4/GPIO5 UART1 path only;
   alternate RP2040 UART pin mappings remain future work.
 * ``IO_BANK0`` stores GPIO function-select, override and interrupt registers,
   implements RP2040 atomic aliases, and gates UART host serial I/O for the
   GPIO0/GPIO1 UART0 path and GPIO4/GPIO5 UART1 path.  It is still a
   simplified routing model: it does not yet derive pad input levels from
   ``PADS_BANK0`` electrical state, does not route general SIO GPIO outputs to
   external pins, and does not connect arbitrary peripheral functions through
   the GPIO matrix.  Edge detection and full interrupt source modelling remain
   future work.
 * SIO divider and interpolator results are computed immediately when their
   registers are accessed.  QEMU does not model the RP2040 single-cycle timing,
   divider latency, or cycle-accurate pipeline effects for these datapaths.
 * ``PADS_BANK0`` and ``PADS_QSPI`` store documented pad-control registers and
   implement RP2040 atomic aliases.  They do not model electrical pad
   behaviour and do not currently gate UART or XIP operation.
 * The XIP cache and detailed timing are not yet modeled.  ``XIP_FLUSH`` is
   treated as immediately complete, and ``XIP_CTR_HIT``/``XIP_CTR_ACC`` do not
   report real cache hit/miss behaviour.  The Pico SDK ``cache_perfctr``
   example is therefore expected to remain a partial validation: ordinary XIP
   execution works, but cache-performance measurements are not meaningful in
   this emulation.  The XIP streaming FIFO is modeled functionally for direct
   reads and DMA from ``XIP_AUX_BASE``, but without flash idle-cycle timing.
   The SSI bulk RX path used by the SDK ``ssi_dma`` example is also modeled
   functionally, including the 32-bit byte order expected with DMA ``BSWAP``,
   but it does not model serial-clock throughput or FIFO refill latency.
   The XIP control and SSI APB register blocks do handle the RP2040 atomic
   ``XOR``/``SET``/``CLR`` aliases.
 * The external flash unique ID is modeled as an 8-byte QEMU property exposed
   through the SPI NOR ``0x4b`` RUID command.  It is stable by default and can
   be overridden with ``flash-uid``; it is not persisted in ``flash-file``.
 * ``IO_QSPI`` stores pin-control and interrupt registers and forwards forced
   ``GPIO_QSPI_SS`` changes to the XIP/SSI model.  It does not emulate the
   electrical QSPI pads or a separate serial bus.
 * The ROSC model exposes stable register behaviour, a nominal clock and a
   QEMU-backed ``RANDOMBIT`` stream.  It does not model analog frequency
   variation or physical oscillator entropy.
 * The synthetic ROM supports direct boot2/application launch, the core1 FIFO
   launch sequence, a subset of the ROM function/data tables, and accelerated
   floating-point and flash helpers.  These helpers are functionally tested
   but intentionally bypass ROM instruction timing and, for flash operations,
   the low-level SSI command sequence.  The external ``-bios`` path can run a
   user-provided real RP2040 mask ROM image, but it is limited by the same
   missing peripheral models as the rest of the machine, notably USB BOOTSEL
   mass-storage mode.
 * ``pico-sdk-exit`` is disabled by default.  When enabled it overlays only
   the initial XIP HardFault vector at ``0x1000010c``.  Faults not caused by
   ``BKPT #0`` are chained to the handler found there when the image was
   loaded.  It does
   not follow a later guest ``VTOR`` relocation or a runtime replacement of
   that vector, and should therefore be used only as a test convenience for
   the Pico SDK exit convention.
 * QEMU does not emulate the Pico BOOTSEL USB mass-storage programming mode.
   UF2 images are accepted only as host-loaded ``-kernel`` inputs.
 * The USB model provides DPRAM, stable VBUS detection, register storage,
   atomic aliases and interrupt mask/force/status calculation.  It does not
   implement packet transactions, endpoint state machines, host mode or a
   BOOTSEL mass-storage device.  PIO and most other peripherals are not yet
   implemented.  DMA supports memory-to-memory transfers, XIP stream and
   XIP/SSI RX DREQ pacing, UART0/UART1 TX/RX DREQ pacing, DMA timer pacing
   from QEMU virtual time, read/write ring wrapping,
   the documented sniff accumulator modes, immediate channel abort, and
   bus-error status reporting through ``CTRL_TRIG``, ``INTR`` and ``INTS0/1``.
   UART DREQs are exposed through the current PL011-backed UART models and
   follow the PL011
   FIFO occupancy plus ``UARTDMACR`` enable bits; fine-grained UART timing is
   not modeled.  DMA timer pacing uses the documented ``X/Y`` fractional timer
   registers and the Pico's nominal 125 MHz system clock as the virtual source.
   DMA bus errors are reported with the documented
   ``READ_ERROR`` or ``WRITE_ERROR`` plus ``AHB_ERROR`` bits, clear ``BUSY``,
   keep the remaining transfer count, and raise the raw channel interrupt.
   QEMU does not model DMA pipeline latency: abort status self-clears
   immediately, and the reported fault address is the exact attempted address
   rather than a delayed approximate address.
 * BUSCTRL stores bus priority and acknowledges updates immediately.  Its four
   performance selectors and counters are programmer-visible, but selected
   counters advance deterministically when read rather than counting actual
   crossbar events.  Bus arbitration priority does not affect memory timing.
 * ``SYSINFO`` and ``SYSCFG`` expose the documented register layout used by
   early firmware.  ``PROC0_NMI_MASK`` is wired for interrupt sources routed
   through the RP2040 IRQ shim, currently including UART0, and
   ``MEMPOWERDOWN`` makes powered-off ROM and SRAM bank windows return memory
   transaction errors.  ``DBGFORCE`` is stored but not connected to an
   SWD/debug fabric model.
 * ``VREG_AND_CHIP_RESET`` stores the voltage-regulator and brown-out detector
   control fields and exposes stable chip reset status.  Analog regulator and
   brown-out behaviour is not modeled.
 * ``TBMAN`` exposes only the documented real-chip ``PLATFORM`` register.
 * ``PSM`` stores force-on, force-off and watchdog-select bits.  ``DONE`` is
   derived immediately from ``FRCE_OFF``; analog power sequencing delays are
   not modeled.
 * The watchdog models ``CTRL``, ``LOAD``, ``REASON``, ``SCRATCH`` and
   ``TICK``, including ``CTRL.TRIGGER`` and the RP2040-E1 double-decrement
   behaviour.  Debug pause inputs are stored but not connected to a debug
   fabric model.
 * The TIMER block models the microsecond counter, ``ALARM0`` through
   ``ALARM3``, ``ARMED``, ``DBGPAUSE``, ``PAUSE`` and the interrupt
   registers.  Alarm outputs are connected to RP2040 IRQs 0 through 3.  The
   counter advances on QEMU virtual time rather than CPU-cycle timing, and
   debug pause inputs have no external debug-fabric side effects.
