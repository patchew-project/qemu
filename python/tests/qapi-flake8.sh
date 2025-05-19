#!/bin/sh -e
python3 -m flake8 ../scripts/qapi/ \
        ../docs/sphinx/qapidoc.py \
        ../docs/sphinx/qapi_domain.py
