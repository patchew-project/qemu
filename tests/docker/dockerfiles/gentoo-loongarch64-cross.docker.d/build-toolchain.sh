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
PORTAGE_SNAPSHOT_FILE=gentoo-20211123.tar.xz
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
    git checkout 075db64f56efab0108f8b82a5868fb58760d54a0
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

# patch Portage tree for linux-headers
pushd /var/db/repos/gentoo

# this is to please checkpatch, hmm...
TAB="$(printf "\t")"
patch -Np1 <<EOF
--- a/eclass/toolchain-funcs.eclass${TAB}2021-11-16 23:28:36.425419786 +0800
+++ b/eclass/toolchain-funcs.eclass${TAB}2021-11-16 23:29:30.378384948 +0800
@@ -675,6 +675,7 @@
 ${TAB}${TAB}${TAB}fi
 ${TAB}${TAB}${TAB};;
 ${TAB}${TAB}ia64*)${TAB}${TAB}echo ia64;;
+${TAB}${TAB}loongarch*)${TAB}ninj loongarch loong;;
 ${TAB}${TAB}m68*)${TAB}${TAB}echo m68k;;
 ${TAB}${TAB}metag*)${TAB}${TAB}echo metag;;
 ${TAB}${TAB}microblaze*)${TAB}echo microblaze;;
@@ -752,6 +753,7 @@
 ${TAB}${TAB}hppa*)${TAB}${TAB}echo big;;
 ${TAB}${TAB}i?86*)${TAB}${TAB}echo little;;
 ${TAB}${TAB}ia64*)${TAB}${TAB}echo little;;
+${TAB}${TAB}loongarch*)${TAB}echo little;;
 ${TAB}${TAB}m68*)${TAB}${TAB}echo big;;
 ${TAB}${TAB}mips*l*)${TAB}echo little;;
 ${TAB}${TAB}mips*)${TAB}${TAB}echo big;;
EOF
unset TAB

popd

# make cross toolchain
crossdev -t "$TRIPLE" --without-headers \
    --binutils 2.37_p1-r1 \
    --gcc 12.0.0_pre9999

# prepare for loongarch cross emerges
pushd "/usr/${TRIPLE}/etc/portage"
    rm make.profile
    ln -s "$LOONGSON_OV"/profiles/desktop/3a5000 ./make.profile

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
