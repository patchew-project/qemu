#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
set -e

if [ -z "$GITLAB_TOKEN" ]; then
    echo "GITLAB_TOKEN is not set. Attempting to fetch via pass..."
    export GITLAB_TOKEN=$(pass gitlab-api)
fi

echo "Fetching labels from qemu-project/qemu..."
# Fetch labels using API and format as "Name        Description"
glab api /projects/qemu-project%2Fqemu/labels --paginate | \
    jq -r '.[] | [ .name, .description ] | @tsv' | \
    column -t -s $'\t' > ../assets/labels.txt

echo "Labels cached in assets/labels.txt"
