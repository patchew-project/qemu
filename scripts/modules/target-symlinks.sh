#!/bin/bash

TARGET_FILE="$1"
shift
TARGET_DIR="$1"
shift

cat > "${TARGET_FILE}" <<EOF
#!/bin/bash

test \$# -eq 1 || exit 1
if [ -z \${MESON_INSTALL_DESTDIR_PREFIX+N} ]; then
    INSTALL_DIR="\${1}"
else
    INSTALL_DIR="\${MESON_INSTALL_DESTDIR_PREFIX}/\${1}"
fi
test -d  "\${INSTALL_DIR}" || exit 1
LINKS_DIR="\${INSTALL_DIR}/${TARGET_DIR}"
mkdir -p \${LINKS_DIR}
# clean up old symbolic links
find \${LINKS_DIR} -iname '*.so' -type l -delete
cd "\${LINKS_DIR}"

EOF
chmod u+x "${TARGET_FILE}"

while test $# -gt 0
do
    echo ln -sfrt \"\$\{LINKS_DIR\}\" \"../"$1"\" >> "${TARGET_FILE}"
    shift
done
