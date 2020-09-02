pacman --noconfirm -S --needed \
base-devel \
git \
mingw-w64-x86_64-python \
mingw-w64-x86_64-python-setuptools \
mingw-w64-x86_64-toolchain \
mingw-w64-x86_64-SDL2 \
mingw-w64-x86_64-SDL2_image \
mingw-w64-x86_64-gtk3 \
mingw-w64-x86_64-ninja \
mingw-w64-x86_64-make \
mingw-w64-x86_64-lzo2 \
mingw-w64-x86_64-libjpeg-turbo \
mingw-w64-x86_64-pixman \
mingw-w64-x86_64-libgcrypt \
mingw-w64-x86_64-capstone \
mingw-w64-x86_64-libpng \
mingw-w64-x86_64-libssh \
mingw-w64-x86_64-libxml2 \
mingw-w64-x86_64-snappy \
mingw-w64-x86_64-libusb \
mingw-w64-x86_64-usbredir \
mingw-w64-x86_64-libtasn1 \
mingw-w64-x86_64-libnfs \
mingw-w64-x86_64-nettle \
mingw-w64-x86_64-cyrus-sasl \
mingw-w64-x86_64-curl \
mingw-w64-x86_64-gnutls \
mingw-w64-x86_64-zstd \
mingw-w64-x86_64-glib2

cd /mingw64/bin
cp x86_64-w64-mingw32-gcc-ar.exe x86_64-w64-mingw32-ar.exe
cp x86_64-w64-mingw32-gcc-ranlib.exe x86_64-w64-mingw32-ranlib.exe
cp x86_64-w64-mingw32-gcc-nm.exe x86_64-w64-mingw32-nm.exe
cp windres.exe x86_64-w64-mingw32-windres.exe
cp strip.exe x86_64-w64-mingw32-strip.exe
cp objcopy.exe x86_64-w64-mingw32-objcopy.exe
cp ld x86_64-w64-mingw32-ld.exe
cp as x86_64-w64-mingw32-as.exe
cp sdl2-config x86_64-w64-mingw32-sdl2-config
