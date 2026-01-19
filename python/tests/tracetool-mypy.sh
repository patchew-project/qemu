#!/bin/sh -e
# SPDX-License-Identifier: GPL-2.0-or-later

cd ../scripts
python3 -m mypy --strict tracetool
