# Overview vhost-user-video

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

[1] https://www.kernel.org/doc/html/latest/userspace-api/media/
    v4l/dev-decoder.html

[2] https://lwn.net/Articles/760650/

# Guest Linux kernel modules
CONFIG_MEDIA_SUPPORT=y
CONFIG_MEDIA_TEST_SUPPORT=y
CONFIG_V4L_TEST_DRIVERS=y
CONFIG_VIRTIO_VIDEO=y
CONFIG_GDB_SCRIPTS=y
CONFIG_DRM_VIRTIO_GPU=y

# Host kernel modules
CONFIG_MEDIA_SUPPORT=y
CONFIG_MEDIA_TEST_SUPPORT=y
CONFIG_V4L_TEST_DRIVERS=y
CONFIG_VIDEO_VICODEC=y

# Run vhost-user-video daemon with vicodec
# (video3 typically is the stateful video)
vhost-user-video --socket-path=/tmp/video.sock --v4l2-device=/dev/video3

# Qemu command for virtio-video device

-device vhost-user-video-pci,chardev=video,id=video
-chardev socket,path=/tmp//video.sock,id=video

# Example v4l2-ctl decode command
wget https://people.linaro.org/~peter.griffin/jelly_640_480-420P.fwht

v4l2-ctl -d0 -x width=640,height=480 -v width=640,height=480,pixelformat=YU12
--stream-mmap --stream-out-mmap --stream-from jelly_640_480-420P.fwht
--stream-to out-jelly-640-480.YU12

# Play the raw decoded video with ffplay or mplayer
ffplay -loglevel warning -v info -f rawvideo -pixel_format  yuv420p
  -video_size "640x480" ./out-jelly-640-480.YU12

mplayer -demuxer rawvideo -rawvideo
  format=i420:w=640:h=480:fps=25 out-jelly-640-480.YU12

# Enable v4l2 debug in virtio-video frontend driver
echo 0x1f > /sys/class/video4linux/video0/dev_debug

# Enable v4l2 debug in vicodec backend driver
echo 0x1f > /sys/class/video4linux/video3/dev_debug

# optee-build system qemu virtio-video command
make QEMU_VIRTFS_ENABLE=y QEMU_USERNET_ENABLE=y CFG_TA_ASLR=n
    QEMU_VHOSTUSER_MEM=y QEMU_VIRTVIDEO_ENABLE=y SSH_PORT_FW=y run-only

Current status
* Tested with v4l2-ctl from v4l2-utils and vicodec stateful decoder driver
* v4l2-compliance - reports
Total: 43, Succeeded: 37, Failed: 6, Warnings: 0

Known Issues
* 6 v4l2-compliance failures remaining
* v4l2-ctl 0fps misleading output
* v4l2-ctl sometimes reports - 0 != <somenumber>
* Encoder not tested yet

TODOs
* Test with a "real" stateful decoder & codec
  (e.g. Qcom Venus or RPi).
* Test more v4l2 userspaces in the guest

Future potential features
* Emulation using libavcodec or similar library
* Support for VAAPI, OpenMax or v4l2 stateless devices
