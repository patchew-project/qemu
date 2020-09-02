export QEMU_DIR=$PWD
mkdir ../qemu-build
cd ../qemu-build
$QEMU_DIR/configure \
  --python=python3 \
  --cross-prefix=x86_64-w64-mingw32- \
  --enable-gtk --enable-sdl \
  --enable-capstone=git \
  --enable-stack-protector \
  --ninja=ninja \
  --enable-gnutls \
  --enable-nettle \
  --enable-vnc \
  --enable-vnc-sasl \
  --enable-vnc-jpeg \
  --enable-vnc-png \
  --enable-membarrier \
  --enable-slirp=git \
  --disable-kvm \
  --enable-hax \
  --enable-whpx \
  --disable-spice \
  --enable-lzo \
  --enable-snappy \
  --enable-bzip2 \
  --enable-vdi \
  --enable-qcow1 \
  --enable-tools \
  --enable-libusb \
  --enable-usb-redir \
  --disable-libnfs \
  --enable-libssh \
  --disable-pie
make -j$NUMBER_OF_PROCESSORS
# make -j$NUMBER_OF_PROCESSORS check
