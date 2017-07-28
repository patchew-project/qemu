#! /bin/bash
#
# This script checks MAINTAINERS consistency
#
# Copyright (C) 2017 Philippe Mathieu-Daudé. GPLv2+.
#
# Usage:
# ./scripts/check_maintainer.sh | tee MAINTAINERS.missing

echo "Incorrect MAINTAINERS paths:" 1>&2
egrep ^F: MAINTAINERS | cut -d\  -f2 | while read p; do
    ls -ld $p 1>/dev/null
done

echo "No maintainers found for:" 1>&2
git ls-files|while read f; do
    OUT=$(./scripts/get_maintainer.pl -f $f 2>&1)
    if [[ "$OUT" == *"No maintainers found"* ]]; then
        echo $f
    fi
done
