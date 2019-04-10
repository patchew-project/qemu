#!/usr/bin/env python

import argparse
from collections import OrderedDict
import json
import re

# Helpers:
def scrub_texi_string(tstring):
    return re.sub(r"@var{([^{}]*?)}", r"\1", tstring)

def human_readable_usage(usage_list):
    return scrub_texi_string(" ".join(usage_list))

def callback_name(command):
    return "img_{:s}".format(command)

# Output modes:
def generate_def_header(conf):
    """Print DEF() macros to be used by qemu-img.c"""
    for command, usage_strs in conf.items():
        print("DEF(\"{}\", {}, \"{} {}\")".format(
            command,
            callback_name(command),
            command,
            human_readable_usage(usage_strs)))

def generate_texi(conf):
    """Generate texi command summary table"""
    print("@table @option")
    for command, usage_strs in conf.items():
        usage = " ".join(usage_strs)
        macro = "qemuimgcmd{}".format(command)
        print("@macro {}".format(macro))
        print("@item {} {}".format(command, usage))
        print("@end macro")
        print("@{}".format(macro))
    print("@end table")

if __name__ == '__main__':
    parser = argparse.ArgumentParser(description="Generate qemu-img command information")
    parser.add_argument("--macros", action="store_true", dest="macros")
    parser.add_argument("--texi", action="store_true")
    parser.add_argument("commands_json")
    args = parser.parse_args()

    with open(args.commands_json, "r") as f:
        conf = json.load(f, object_pairs_hook=OrderedDict)

    if args.macros:
        generate_def_header(conf)
    if args.texi:
        generate_texi(conf)
