mkdir build
cd build
../configure \
  --python=python3 \
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
  --enable-libnfs \
  --enable-libssh \
  --disable-pie
make -j$NUMBER_OF_PROCESSORS
# make check
