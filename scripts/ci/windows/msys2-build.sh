mkdir build
cd build
../configure \
--python=python3 \
--ninja=ninja \
--enable-stack-protector \
--enable-guest-agent \
--disable-pie \
--enable-gnutls --enable-nettle \
--enable-sdl --enable-sdl-image --enable-gtk --disable-vte --enable-curses --enable-iconv \
--enable-vnc --enable-vnc-sasl --enable-vnc-jpeg --enable-vnc-png \
--enable-slirp=git \
--disable-brlapi --enable-curl \
--enable-fdt \
--disable-kvm --enable-hax --enable-whpx \
--enable-libnfs --enable-libusb --enable-live-block-migration --enable-usb-redir \
--enable-lzo --enable-snappy --enable-bzip2 --enable-zstd \
--enable-membarrier --enable-coroutine-pool \
--enable-libssh --enable-libxml2 \
--enable-jemalloc --enable-avx2 \
--enable-replication \
--enable-tools \
--enable-bochs --enable-cloop --enable-dmg --enable-qcow1 --enable-vdi --enable-vvfat --enable-qed --enable-parallels \
--enable-sheepdog \
--enable-capstone=git

make -j$NUMBER_OF_PROCESSORS
make -i -j$NUMBER_OF_PROCESSORS check
