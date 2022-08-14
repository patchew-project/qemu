# QEMU LoongArch target

## Introduction

LoongArch is the general-purpose instruction set architecture developed by
Loongson. Documentation can be found at [the LoongArch Documentation repository][docs-repo].

[docs-repo]: https://github.com/loongson/LoongArch-Documentation

Currently the following CPU models are supported:

|`-cpu name`|Description|
|:----------|:----------|
|`qemu64-v1.00`|Virtual model similar to the Loongson 3A5000, compatible with LoongArch64 v1.00.|

## Trying out

For some of the steps or development/debug purposes, you may have to set up
cross toolchains if you are not running on native LoongArch hardware.

Now that LoongArch support has been merged in the GNU toolchain packages and
Linux, you may make your own toolchains like with any other architectures;
Loongson also has made available [their pre-compiled toolchain binaries for x86][build-tools].
You may follow the respective instructions to set up your development
environment.

[build-tools]: https://github.com/loongson/build-tools

### System emulation

Both raw ELF images and EFI stub kernels together with UEFI firmware image are
supported.

For running raw ELF images with system emulation:

```sh
# In the build directory:
./qemu-system-loongarch64 -m 4G -smp 1 \
    -kernel build/tests/tcg/loongarch64-softmmu/hello \
    -monitor none -display none \
    -chardev file,path=hello.out,id=output -serial chardev:output
```

For a more complete emulation with UEFI firmware, currently there is no
pre-compiled firmware blob yet, but in the meantime you may compile your own
firmware image with Loongson's forked [EDK II][edk2] and
[corresponding platform code][edk2-plat].

[edk2]: https://github.com/loongson/edk2-LoongarchVirt
[edk2-plat]: https://github.com/loongson/edk2-platforms

Once you have the firmware image in place, you could boot EFI images with it.
For example:

```sh
./qemu-system-loongarch64 -m 4G smp 4 \
    -bios path/to/your/QEMU_EFI.fd \
    -kernel path/to/your/vmlinux \
    -initrd path/to/your/initramfs/if/you/have/one \
    -append 'root=/dev/ram rdinit=/sbin/init console=ttyS0,115200'
    -nographic
```

### Linux-user emulation

Linux-user emulation is fully supported, and there are already several Linux
distributions available with ready-to-use sysroot tarballs, for example
[CLFS][clfs] and [Gentoo][gentoo].

You may compile static qemu-user binaries then follow suitable instructions
for your distribution (set up binfmt\_misc, etc.) to make yourself a LoongArch
chroot for experimentation.

[clfs]: https://github.com/sunhaiyong1978/CLFS-for-LoongArch
[gentoo]: https://wiki.gentoo.org/wiki/Project:LoongArch
