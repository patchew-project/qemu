#!/bin/bash

set -e

TRIPLE=loongarch64-unknown-linux-gnu
CROSSDEV_OV=/opt/crossdev-overlay
LOONGSON_OV=/opt/loongson-overlay
CROSS_EMERGE="${TRIPLE}-emerge"

# this will break on non-SMP machines, but no one should build this image
# on such machine in the first place
J=$(expr $(nproc) / 2)
echo "MAKEOPTS=\"-j${J} -l${J}\"" >> /etc/portage/make.conf
echo "EGIT_CLONE_TYPE=shallow" >> /etc/portage/make.conf

# these features are not supported in Docker
export FEATURES="-ipc-sandbox -network-sandbox"

# populate Portage tree
GENTOO_MIRROR='https://bouncer.gentoo.org/fetch/root/all'
PORTAGE_SNAPSHOT_FILE=gentoo-20211214.tar.xz
pushd /tmp
    wget "${GENTOO_MIRROR}/snapshots/${PORTAGE_SNAPSHOT_FILE}"

    mkdir -p /var/db/repos/gentoo
    pushd /var/db/repos/gentoo
        tar -xf "/tmp/${PORTAGE_SNAPSHOT_FILE}" --strip-components=1
    popd

    rm "$PORTAGE_SNAPSHOT_FILE"
popd

emerge -j crossdev dev-vcs/git

# prepare for crossdev
mkdir /etc/portage/repos.conf
crossdev -t "$TRIPLE" --ov-output "$CROSSDEV_OV" --init-target

git clone https://github.com/xen0n/loongson-overlay.git "$LOONGSON_OV"
pushd "$LOONGSON_OV"
    git checkout 4bf659f8f6a24794cfd34cc7724bbfc965817689
popd

pushd "${CROSSDEV_OV}/cross-${TRIPLE}"
    rm binutils gcc glibc linux-headers
    ln -s "${LOONGSON_OV}/sys-devel/binutils" .
    ln -s "${LOONGSON_OV}/sys-devel/gcc" .
    ln -s "${LOONGSON_OV}/sys-libs/glibc" .
    ln -s "${LOONGSON_OV}/sys-kernel/linux-headers" .
popd

cat > "${CROSSDEV_OV}/metadata/layout.conf" <<EOF
masters = gentoo
repo-name = crossdev-overlay
manifest-hashes = SHA256 SHA512 WHIRLPOOL
thin-manifests = true
EOF

chown -R portage:portage "$CROSSDEV_OV"
chown -R portage:portage "$LOONGSON_OV"

# toolchain-funcs.eclass patch is upstream as of 20211214 snapshot,
# but we still need to patch multilib.eclass for correct libpath
pushd /var/db/repos/gentoo

# this is to please checkpatch, hmm...
TAB="$(printf "\t")"
patch -Np1 <<EOF
--- a/eclass/multilib.eclass
+++ b/eclass/multilib.eclass
@@ -368,6 +368,15 @@ multilib_env() {
 ${TAB}${TAB}${TAB}${TAB};;
 ${TAB}${TAB}${TAB}esac
 ${TAB}${TAB};;
+${TAB}${TAB}loongarch64*)
+${TAB}${TAB}${TAB}export CFLAGS_lp64d=\${CFLAGS_lp64d--mabi=lp64d}
+${TAB}${TAB}${TAB}export CHOST_lp64d=\${CTARGET}
+${TAB}${TAB}${TAB}export CTARGET_lp64d=\${CTARGET}
+${TAB}${TAB}${TAB}export LIBDIR_lp64d=\${LIBDIR_lp64d-lib64}
+
+${TAB}${TAB}${TAB}: \${MULTILIB_ABIS=lp64d}
+${TAB}${TAB}${TAB}: \${DEFAULT_ABI=lp64d}
+${TAB}${TAB};;
 ${TAB}${TAB}mips64*|mipsisa64*)
 ${TAB}${TAB}${TAB}export CFLAGS_o32=\${CFLAGS_o32--mabi=32}
 ${TAB}${TAB}${TAB}export CHOST_o32=\${CTARGET/mips64/mips}
EOF
unset TAB

popd

# make cross toolchain
crossdev -t "$TRIPLE" --without-headers \
    --binutils 2.37_p1-r1 \
    --gcc 12.0.0_pre9999 \
    --libc 2.34-r4

# prepare for loongarch cross emerges
TARGET_PROFILE="default/linux/loong/21.0/la64v100/lp64d/desktop"
pushd "/usr/${TRIPLE}/etc/portage"
    rm make.profile
    ln -s "${LOONGSON_OV}/profiles/${TARGET_PROFILE}" ./make.profile

    mkdir repos.conf
    cat > repos.conf/loongson.conf <<EOF
[loongson]
priority = 50
location = $LOONGSON_OV
auto-sync = No
EOF

popd

# add build deps for qemu

# gawk seems to have problems installing with concurrency, and its deps
# include ncurses that needs disabling sandbox to correctly build under
# Docker, so just turn off sandbox for all emerges
export FEATURES="$FEATURES -sandbox -usersandbox"
$CROSS_EMERGE -1 --onlydeps -j$J gawk
mkdir -p "/usr/${TRIPLE}/bin"
MAKEOPTS='-j1' $CROSS_EMERGE -1 gawk

# then build the rest
$CROSS_EMERGE -1 --onlydeps -j$J qemu

# clean up downloaded files and Portage tree for smaller image
rm -rf /var/db/repos/gentoo /var/cache/distfiles
