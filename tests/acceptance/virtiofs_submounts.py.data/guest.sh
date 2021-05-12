#!/bin/bash

function print_usage()
{
    if [ -n "$2" ]; then
        echo "Error: $2"
        echo
    fi
    echo "Usage: $1 <shared dir>"
    echo '(The shared directory is the "share" directory in the scratch' \
         'directory)'
}

shared_dir=$1
if [ -z "$shared_dir" ]; then
    print_usage "$0" 'Shared dir not given' >&2
    exit 1
fi

cd "$shared_dir"

# See whether `find` complains about anything, like file system loops,
# by looking for a file that does not exist (so the output should be
# empty).
# (Historically, for mount points, virtiofsd reported only the inode ID
# in submount, i.e. the submount root's inode ID.  However, while the
# submount is not yet auto-mounted in the guest, it would have the
# parent's device ID, and so would have the same st_dev/st_ino
# combination as the parent filesystem's root.  This would lead to
# `find` reporting file system loops.
# This has been fixed so that virtiofsd reports the mount point node's
# inode ID in the parent filesystem, and when the guest auto-mounts the
# submount, it will only then see the inode ID in that FS.)
#
# As a side-effect, this `find` auto-mounts all submounts by visiting
# the whole tree.
find_output=$(find -name there-is-no-such-file 2>&1)
if [ -n "$find_output" ]; then
    echo "Error: find has reported errors or warnings:" >&2
    echo "$find_output" >&2
    exit 1
fi

if [ -n "$(find -name not-mounted)" ]; then
    echo "Error: not-mounted files visible on mount points:" >&2
    find -name not-mounted >&2
    exit 1
fi

if [ ! -f some-file -o "$(cat some-file)" != 'root' ]; then
    echo "Error: Bad file in the share root" >&2
    exit 1
fi

shopt -s nullglob

function check_submounts()
{
    local base_path=$1

    for mp in mnt*; do
        printf "Checking submount %i...\r" "$((${#devs[@]} + 1))"

        mp_i=$(echo "$mp" | sed -e 's/mnt//')
        dev=$(stat -c '%D' "$mp")

        if [ -n "${devs[mp_i]}" ]; then
            echo "Error: $mp encountered twice" >&2
            exit 1
        fi
        devs[mp_i]=$dev

        pushd "$mp" >/dev/null
        path="$base_path$mp"
        while true; do
            expected_content="$(printf '%s\n%s\n' "$mp_i" "$path")"
            if [ ! -f some-file ]; then
                echo "Error: $PWD/some-file does not exist" >&2
                exit 1
            fi

            if [ "$(cat some-file)" != "$expected_content" ]; then
                echo "Error: Bad content in $PWD/some-file:" >&2
                echo '--- found ---'
                cat some-file
                echo '--- expected ---'
                echo "$expected_content"
                exit 1
            fi
            if [ "$(stat -c '%D' some-file)" != "$dev" ]; then
                echo "Error: $PWD/some-file has the wrong device ID" >&2
                exit 1
            fi

            if [ -d sub ]; then
                if [ "$(stat -c '%D' sub)" != "$dev" ]; then
                    echo "Error: $PWD/some-file has the wrong device ID" >&2
                    exit 1
                fi
                cd sub
                path="$path/sub"
            else
                if [ -n "$(echo mnt*)" ]; then
                    check_submounts "$path/"
                fi
                break
            fi
        done
        popd >/dev/null
    done
}

root_dev=$(stat -c '%D' some-file)
devs=()
check_submounts ''
echo

reused_devs=$(echo "$root_dev ${devs[@]}" | tr ' ' '\n' | sort | uniq -d)
if [ -n "$reused_devs" ]; then
    echo "Error: Reused device IDs: $reused_devs" >&2
    exit 1
fi

echo "Test passed for ${#devs[@]} submounts."
