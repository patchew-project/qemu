#!/usr/bin/env python3
# -*- coding: utf-8 -*-

"""
This script creates wrapper binaries that invoke the general-device-fuzzer with
configurations specified in a yaml config file.
"""
import sys
import os
import yaml
import tempfile

CC = ""
TEMPLATE = ""


def usage():
    print("Usage: CC=COMPILER {} CONFIG_PATH \
OUTPUT_PATH_PREFIX".format(sys.argv[0]))
    sys.exit(0)


def str_to_c_byte_array(s):
    """
    Convert strings to byte-arrays so we don't worry about formatting
    strings to play nicely with cc -DQEMU_FUZZARGS etc
    """
    return ','.join('0x{:02x}'.format(ord(x)) for x in s)


def compile_wrapper(cfg, path):
    os.system('$CC -DQEMU_FUZZ_ARGS="{}" -DQEMU_FUZZ_OBJECTS="{}" \
                {} -o {}'.format(
                    str_to_c_byte_array(cfg["args"].replace("\n", " ")),
                    str_to_c_byte_array(cfg["objects"].replace("\n", " ")),
                    TEMPLATE, path))


def main():
    global CC
    global TEMPLATE

    if len(sys.argv) != 3:
        usage()

    cfg_path = sys.argv[1]
    out_path = sys.argv[2]

    CC = os.getenv("CC")
    TEMPLATE = os.path.join(os.path.dirname(__file__), "target.c")

    with open(cfg_path, "r") as f:
        configs = yaml.load(f)["configs"]
    for cfg in configs:
        assert "name" in cfg
        assert "args" in cfg
        assert "objects" in cfg
        compile_wrapper(cfg, out_path + cfg["name"])


if __name__ == '__main__':
    main()
