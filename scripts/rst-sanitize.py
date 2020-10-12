#!/usr/bin/env python3

#
# Script for remove cr line ending in file
#
# Authors:
#  Yonggang Luo <luoyonggang@gmail.com>
#
# This work is licensed under the terms of the GNU GPL, version 2
# or, at your option, any later version.  See the COPYING file in
# the top-level directory.

import sys

def main(_program, file, *unused):
    with open(file, 'rb') as content_file:
        content = content_file.read()
        sys.stdout.buffer.write(content.replace(b'\r', b''))

if __name__ == "__main__":
    main(*sys.argv)
