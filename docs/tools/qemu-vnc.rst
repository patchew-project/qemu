=============================
QEMU standalone VNC server
=============================

Synopsis
--------

**qemu-vnc** [*OPTION*]

Description
-----------

``qemu-vnc`` is a standalone VNC server that connects to a running QEMU
instance via the D-Bus display interface
(:ref:`dbus-display`).  It re-exports the
guest display, keyboard, mouse, audio, clipboard, and serial console
chardevs over the VNC protocol, allowing VNC clients to interact with
the virtual machine without QEMU itself binding a VNC socket.

The server connects to a QEMU instance that has been started with
``-display dbus`` and registers as a D-Bus display listener.

The following features are supported:

* Graphical console display (scanout and incremental updates)
* Shared-memory scanout via Unix file-descriptor passing
* Hardware cursor
* Keyboard input (translated to QEMU key codes)
* Absolute and relative mouse input
* Mouse button events
* Audio playback forwarding to VNC clients
* Clipboard sharing (text) between guest and VNC client
* Serial console chardevs exposed as VNC text consoles
* TLS encryption (x509 credentials)
* VNC password authentication (``--password`` flag or systemd credentials)
* Lossy (JPEG) compression
* WebSocket transport

Options
-------

.. program:: qemu-vnc

.. option:: -a ADDRESS, --dbus-address=ADDRESS

  D-Bus address to connect to.  When not specified, ``qemu-vnc``
  connects to the session bus.

.. option:: -p FD, --dbus-p2p-fd=FD

  File descriptor of an inherited Unix socket for a peer-to-peer D-Bus
  connection to QEMU.  This is mutually exclusive with
  ``--dbus-address`` and ``--bus-name``.

.. option:: -n NAME, --bus-name=NAME

  D-Bus bus name of the QEMU instance to connect to.  The default is
  ``org.qemu``.  When a custom ``--dbus-address`` is given without a
  bus name, peer-to-peer D-Bus is used.

.. option:: -c N, --console=N

  Console number to attach to (default 0).

.. option:: -l ADDR, --vnc-addr=ADDR

  VNC listen address in the same format as the QEMU ``-vnc`` option
  (default ``localhost:0``, i.e. TCP port 5900).

.. option:: -w ADDR, --websocket=ADDR

  Enable WebSocket transport on the given address.  *ADDR* can be a
  port number or an *address:port* pair.

.. option:: -t DIR, --tls-creds=DIR

  Directory containing TLS x509 credentials (``ca-cert.pem``,
  ``server-cert.pem``, ``server-key.pem``).  When specified, the VNC
  server requires TLS from connecting clients.

.. option:: -s POLICY, --share=POLICY

  Set display sharing policy.  *POLICY* is one of
  ``allow-exclusive``, ``force-shared``, or ``ignore``.

  ``allow-exclusive`` allows clients to ask for exclusive access.
  As suggested by the RFB spec this is implemented by dropping other
  connections.  Connecting multiple clients in parallel requires all
  clients asking for a shared session (vncviewer: -shared switch).
  This is the default.

  ``force-shared`` disables exclusive client access.  Useful for
  shared desktop sessions, where you don't want someone forgetting to
  specify -shared disconnect everybody else.

  ``ignore`` completely ignores the shared flag and allows everybody
  to connect unconditionally.  Doesn't conform to the RFB spec but
  is traditional QEMU behavior.

.. option:: -k LAYOUT, --keyboard-layout=LAYOUT

  Keyboard layout (e.g. ``en-us``).  Passed through to the VNC server
  for key-code translation.

.. option:: -C NAME, --vt-chardev=NAME

  Chardev D-Bus name to expose as a VNC text console.  This option may
  be given multiple times to expose several chardevs.  When not
  specified, the defaults ``org.qemu.console.serial.0`` and
  ``org.qemu.monitor.hmp.0`` are used.

.. option:: -N, --no-vt

  Do not expose any chardevs as text consoles.  This overrides the
  default chardev list and any ``--vt-chardev`` options.

.. option:: -T PATTERN, --trace=PATTERN

  Trace options, same syntax as the QEMU ``-trace`` option.

.. option:: --password

  Require VNC password authentication from connecting clients.  The
  password is set at runtime via the D-Bus ``SetPassword`` method (see
  :doc:`/interop/dbus-vnc`).  Clients will not be able to connect
  until a password has been set.

  This option is ignored when a systemd credential password is
  present, since password authentication is already enabled via
  ``password-secret`` in that case.

.. option:: --lossy

  Enable  lossy  compression methods (gradient, JPEG, ...). If this option
  is set, VNC client may receive lossy framebuffer updates depending on its
  encoding settings. Enabling this option can save a lot of bandwidth at
  the expense of quality.

.. option:: --non-adaptive

  Disable adaptive encodings. Adaptive encodings are enabled by default.
  An adaptive encoding will try to detect frequently updated screen regions,
  and send updates in these  regions  using  a lossy encoding (like JPEG).
  This can be really helpful to save bandwidth when playing videos.
  Disabling adaptive encodings restores the original static behavior of
  encodings like Tight.

.. option:: -V, --version

  Print version information and exit.

Examples
--------

Start QEMU with the D-Bus display backend::

    qemu-system-x86_64 -display dbus -drive file=disk.qcow2

Then attach ``qemu-vnc``::

    qemu-vnc

A VNC client can now connect to ``localhost:5900``.

To listen on a different port with TLS::

    qemu-vnc --vnc-addr localhost:1 --tls-creds /etc/pki/qemu-vnc

To connect to a specific D-Bus address (peer-to-peer)::

    qemu-vnc --dbus-address unix:path=/tmp/qemu-dbus.sock

VNC Password Authentication
~~~~~~~~~~~~~~~~~~~~~~~~~~~~

There are two ways to enable VNC password authentication:

1. **``--password`` flag** — start ``qemu-vnc`` with ``--password`` and
   then set the password at runtime using the D-Bus ``SetPassword``
   method.  Clients will be rejected until a password is set.

2. **systemd credentials** — if the ``CREDENTIALS_DIRECTORY``
   environment variable is set (see :manpage:`systemd.exec(5)`) and
   contains a file named ``vnc-password``, the VNC server will use
   that file's contents as the password automatically.  The
   ``--password`` flag is not needed in this case.

D-Bus interface
---------------

``qemu-vnc`` exposes a D-Bus interface for management and monitoring of
VNC connections.  See :doc:`/interop/dbus-vnc` for the full interface
reference.

See also
--------

:manpage:`qemu(1)`,
`The RFB Protocol <https://github.com/rfbproto/rfbproto>`_
