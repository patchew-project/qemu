=====================
QEMU vhost-user-video
=====================

Overview
--------

This vmm translates from virtio-video v3 protocol and writes
to a v4l2 mem2mem stateful decoder/encoder device [1]. v3 was
chosen as that is what the virtio-video Linux frontend driver
currently implements.

The primary goal so far is to enable development of virtio-video
frontend driver using purely open source software. Using vicodec
v4l2 stateful decoder on the host for testing then allows a pure
virtual environment for development and testing.

Currently the vmm only supports v4l2 stateful devices, and the
intention is it will be used with Arm SoCs that implement stateful
decode/encode devices such as Qcom Venus, RPi, MediaTek etc.

A Qemu + vicodec setup for virtio-video should also allow for
CI systems like kernelci, lkft to test the virtio-video interface
easily.

Currently support for VAAPI or decoding via libavcodec or similar
libraries is not implemented, but this could be added in the future.

Some example commands are provided below on how to run the daemon
and achieve a video decode using vicodec and a link to some test
content.

[1] https://www.kernel.org/doc/html/latest/userspace-api/media/v4l/dev-decoder.html

[2] https://lwn.net/Articles/760650/

Examples
--------

Guest Linux kernel modules:

::

    CONFIG_MEDIA_SUPPORT=y
    CONFIG_MEDIA_TEST_SUPPORT=y
    CONFIG_V4L_TEST_DRIVERS=y
    CONFIG_VIRTIO_VIDEO=y
    CONFIG_GDB_SCRIPTS=y
    CONFIG_DRM_VIRTIO_GPU=y

Host kernel modules:

::

    CONFIG_MEDIA_SUPPORT=y
    CONFIG_MEDIA_TEST_SUPPORT=y
    CONFIG_V4L_TEST_DRIVERS=y
    CONFIG_VIDEO_VICODEC=y

Note: Vicodec has been recently included in the Fedora kernel releases,
but it is not yet set on the default Debian kernel.

The daemon should be started first (video3 typically is the stateful video):

::

    host# vhost-user-video --socket-path=/tmp/video.sock --v4l2-device=/dev/video3

The QEMU invocation needs to create a chardev socket the device can
use to communicate as well as share the guests memory over a memfd.

::

    host# qemu-system								\
        -device vhost-user-video-pci,chardev=video,dev_type=decoder,id=video    \
        -chardev socket,path=/tmp//video.sock,id=video                          \
        -m 4096 		        					\
        -object memory-backend-file,id=mem,size=4G,mem-path=/dev/shm,share=on	\
        -numa node,memdev=mem							\
        ...

After booting, the device should be available at /dev/video0:

::

    guest# v4l2-ctl -d/dev/video0 --info
    Driver Info:
            Driver name      : virtio-video
            Card type        : 
            Bus info         : virtio:stateful-decoder
            Driver version   : 6.1.0
            Capabilities     : 0x84204000
                    Video Memory-to-Memory Multiplanar
                    Streaming
                    Extended Pix Format
                    Device Capabilities
            Device Caps      : 0x04204000
                    Video Memory-to-Memory Multiplanar
                    Streaming
                    Extended Pix Format

Example v4l2-ctl decode command:

::

    guest# wget https://people.linaro.org/~peter.griffin/jelly_640_480-420P.fwht
    guest# v4l2-ctl -d0 -x width=640,height=480 -v width=640,height=480,pixelformat=YU12 \
        --stream-mmap --stream-out-mmap --stream-from jelly_640_480-420P.fwht            \
        --stream-to out-jelly-640-480.YU12

Play the raw decoded video with ffplay or mplayer

::

    guest# ffplay -loglevel warning -v info -f rawvideo -pixel_format yuv420p \
        -video_size "640x480" ./out-jelly-640-480.YU12
    guest# mplayer -demuxer rawvideo -rawvideo \
        format=i420:w=640:h=480:fps=25 out-jelly-640-480.YU12

Enable v4l2 debug in virtio-video driver

::

    # echo 0x1f > /sys/class/video4linux/videoX/dev_debug
