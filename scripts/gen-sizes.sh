#!/bin/sh

size_suffix() {
    case ${1} in
        1)
            printf "KiB"
            ;;
        2)
            printf "MiB"
            ;;
        3)
            printf "GiB"
            ;;
        4)
            printf "TiB"
            ;;
        5)
            printf "PiB"
            ;;
        6)
            printf "EiB"
            ;;
    esac
}

print_sizes() {
    local p=10
    while [ ${p} -lt 64 ]
    do
        local pad=' '
        local n=$((p % 10))
        n=$((1 << n))
        [ $((n / 100)) -eq 0 ] && pad='  '
        [ $((n / 10)) -eq 0 ] && pad='   '
        local suff=$((p / 10))
        printf "#define S_%u%s%s%20u\n" ${n} "$(size_suffix ${suff})" \
            "${pad}" $((1 << p))
        p=$((p + 1))
    done
}

print_header() {
    cat <<EOF
/*
 * The following lookup table is intended to be used when a literal string of
 * the number of bytes is required (for example if it needs to be stringified).
 * It can also be used for generic shortcuts of power-of-two sizes.
 * This table is generated automatically during the build.
 *
 * Author:
 *   Leonid Bloch  <lbloch@janustech.com>
 */

#ifndef QEMU_SIZES_H
#define QEMU_SIZES_H

EOF
}

print_footer() {
    printf "\n#endif\n"
}

print_header  > "${1}"
print_sizes  >> "${1}"
print_footer >> "${1}"
