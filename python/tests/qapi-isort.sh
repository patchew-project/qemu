#!/bin/sh -e
# SPDX-License-Identifier: GPL-2.0-or-later

python3 -m isort --sp . -c ../scripts/qapi/
# Force isort to recognize "qapidoc_legacy" as a local module
python3 -m isort --sp . -c -p qapidoc_legacy \
        ../docs/sphinx/qapi_domain.py \
        ../docs/sphinx/qapidoc.py
