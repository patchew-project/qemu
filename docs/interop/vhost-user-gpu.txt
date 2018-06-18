=======================
Vhost-user-gpu Protocol
=======================

This work is licensed under the terms of the GNU GPL, version 2 or later.
See the COPYING file in the top-level directory.

Overview
========

The vhost-user-gpu protocol is aiming at sharing the rendering result
of a virtio-gpu, done from a vhost-user slave process to a vhost-user
master process (such as QEMU). It bears a resemblance to a display
server protocol, if you consider QEMU as the display server and the
slave as the client, but in a very limited way. Typically, it will
work by setting a scanout/display configuration, before sending flush
events for the display updates. It will also update the cursor shape
and position.

The protocol is sent over a UNIX domain stream socket, since it uses
socket ancillary data to share opened file descriptors (DMABUF fds or
shared memory).

Requests are sent by the slave, and the optional replies by the master.

Wire format
===========

Unless specified differently, numbers are in the machine native byte
order.

A vhost-user-gpu request consists of 2 header fields and a payload:

::

  ------------------------------------
  | u32:request | u32:size | payload |
  ------------------------------------

- request: 32-bit type of the request
- size: 32-bit size of the payload

A reply consists only of a payload, whose content depends on on the request.


Payload types
-------------

VhostUserGpuCursorPos
^^^^^^^^^^^^^^^^^^^^^

::

   ----------------------------------
   | u32:scanout-id | u32:x | u32:y |
   ----------------------------------

- scanout-id: the scanout where the cursor is located
- x/y: the cursor postion

VhostUserGpuCursorUpdate
^^^^^^^^^^^^^^^^^^^^^^^^

::

   -----------------------------------------------------------------------------
   | VhostUserGpuCursorPos:pos | u32:hot_x | u32:hot_y | [u32; 64 * 64] cursor |
   -----------------------------------------------------------------------------

- pos: the cursor location
- hot_x/hot_y: the cursor hot location
- cursor: RGBA cursor data

VhostUserGpuScanout
^^^^^^^^^^^^^^^^^^^

::

   ----------------------------------
   | u32:scanout-id | u32:w | u32:h |
   ----------------------------------

- scanout-id: the scanout configuration to set
- w/h: the scanout width/height size


VhostUserGpuUpdate
^^^^^^^^^^^^^^^^^^

::

   ---------------------------------------------------------
   | u32:scanout-id | u32:x | u32:y | u32:w | u32:h | data |
   ---------------------------------------------------------

- scanout-id: the scanout content to update
- x/y/w/h: region of the update
- data: RGBA data (size is computed based on the region size, and request type)

VhostUserGpuDMABUFScanout
^^^^^^^^^^^^^^^^^^^^^^^^^

::

   ---------------------------------------------------
   | u32:scanout-id | u32:x | u32:y | u32:w | u32:h | ...
   ----------------------------------------------------------
    u32:fdw | u32:fwh | u32:stride | u32:flags | i32:fourcc |
   ----------------------------------------------------------

- scanout-id: the scanout configuration to set
- x/y: the location of the scanout within the DMABUF
- w/h: the scanout width/height size
- fdw/fdh/stride/flags/fourcc: the DMABUF width/height/stride/flags/drm-fourcc


In QEMU the vhost-user-gpu message is implemented with the following struct:

::

  typedef struct VhostUserGpuMsg {
      uint32_t request; /* VhostUserGpuRequest */
      uint32_t size; /* the following payload size */
      union {
          VhostUserGpuCursorPos cursor_pos;
          VhostUserGpuCursorUpdate cursor_update;
          VhostUserGpuScanout scanout;
          VhostUserGpuUpdate update;
          VhostUserGpuDMABUFScanout dmabuf_scanout;
          uint64_t u64;
      } payload;
  } QEMU_PACKED VhostUserGpuMsg;

Protocol features
=================

None yet.

As the protocol may need to evolve, new messages and communication
changes are negotiated thanks to preliminary
VHOST_USER_GPU_GET_PROTOCOL_FEATURES and
VHOST_USER_GPU_SET_PROTOCOL_FEATURES requests.

Message types
=============

- VHOST_USER_GPU_GET_PROTOCOL_FEATURES

  Id:1
  Request payload: N/A
  Reply payload: uint64_t

  Get the supported protocol features bitmask.

- VHOST_USER_GPU_SET_PROTOCOL_FEATURES

  Id:2
  Request payload: uint64_t
  Reply payload: N/A

  Enable protocol features using a bitmask.

- VHOST_USER_GPU_GET_DISPLAY_INFO

  Id:3
  Request payload: N/A
  Reply payload: struct virtio_gpu_resp_display_info (numbers in LE,
                 according to the virtio protocol)

  Get the preferred display configuration.

- VHOST_USER_GPU_CURSOR_POS

  Id: 4
  Request payload: struct VhostUserGpuCursorPos
  Reply payload: N/A

  Set/show the cursor position.

- VHOST_USER_GPU_CURSOR_POS_HIDE

  Id:5
  Request payload: struct VhostUserGpuCursorPos
  Reply payload: N/A

  Set/hide the cursor.

- VHOST_USER_GPU_CURSOR_UPDATE

  Id:6
  Request payload: struct VhostUserGpuCursorUpdate
  Reply payload: N/A

  Update the cursor shape and location.

- VHOST_USER_GPU_SCANOUT

  Id:7
  Request payload: struct VhostUserGpuScanout
  Reply payload: N/A

  Set the scanout resolution. To disable a scanout, the dimensions
  width/height are set to 0.

- VHOST_USER_GPU_UPDATE

  Id:8
  Request payload: struct VhostUserGpuUpdate
  Reply payload: N/A

  Update the scanout content. The data payload contains the graphical bits.
  The display should be flushed and presented.

- VHOST_USER_GPU_DMABUF_SCANOUT

  Id:9
  Request payload: struct VhostUserGpuDMABUFScanout
  Reply payload: N/A

  Set the scanout resolution/configuration, and share a DMABUF file
  descriptor for the scanout content, which is passed as ancillary
  data. To disable a scanout, the dimensions width/height are set
  to 0, there is no file descriptor passed.

- VHOST_USER_GPU_DMABUF_UPDATE

  Id:10
  Request payload: struct VhostUserGpuUpdate
  Reply payload: u32

  The display should be flushed and presented according to updated
  region from VhostUserGpuUpdate.

  Note: there is no data payload, since the scanout is shared thanks
  to DMABUF, that must have been set previously with
  VHOST_USER_GPU_DMABUF_SCANOUT.
