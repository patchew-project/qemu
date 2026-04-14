# OpenTitan UART Support

## Connecting to the UART

* `-serial mon:stdio`, used as the first `-serial` option, redirects the virtual UART0 to the
  current console/shell.

* `-chardev socket,id=serial1,host=localhost,port=8001,server=on,wait=off` and
  `-serial chardev:serial1` can be used to redirect UART1 (in this example) to a TCP socket. These
  options are not specific to OpenTitan emulation, but are useful to communicate over a UART.
  Note that QEMU offers many `chardev` backends, please check QEMU documentation for details.

## Sending Break Conditions

Break conditions can be sent to the UART on select supported CharDev backends (telnet, mux)
or by sending the `chardev-send-break` command with the CharDev ID via the QEMU Monitor.
Break conditions are treated as transient events and the length of time of a break condition
is not considered.

## Oversampling

OpenTitan's UART has a `VAL` register which oversamples the RX pin 16 times per bit.
This cannot be emulated by QEMU which uses a CharDev backend and does not have a notion of
accurate sampling times.

If software wishes to poll the `VAL` register to determine break conditions, there are
some properties available to help with emulating this use case:

* `-global ot-uart.oversample-break=true` is used to enable UART break oversampling.
  This will attempt to display 16 samples of the last bit received in the `VAL` register,
  which will be 16 high bits after any UART frame is transmitted (as these end with a stop
  bit, which is high), or 16 low bits if the UART previously received a break condition
  and has not received any frames since. That is, enabling this property assumes that
  transmitted break conditions are "held" until the next UART transfer in terms of what
  is being shown in the oversampled `VAL` register.

* `-global ot-uart.toggle-break=true` is used to provide more control over "holding"
  the UART RX break condition like a GPIO strap, and changes the behavior of a UART
  such that received break condition events now *toggle* the break condition state
  rather than keeping it asserted until the next transfer. This allows any device talking
  to OpenTitan via UART to have more precise control over when the UART VAL register
  displays idle and when it displays a break condition, as it can precisely toggle the
  break condition on or off like a GPIO strapping being held down.
