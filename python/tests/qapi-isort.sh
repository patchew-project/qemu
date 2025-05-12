#!/bin/sh -e
python3 -m isort --sp . -c ../scripts/qapi/
# Force isort to recognize "compat" as a local module and not third-party
python3 -m isort --sp . -c -p compat -p qapidoc_legacy \
        ../docs/sphinx/qapi_domain.py \
        ../docs/sphinx/qapidoc.py
