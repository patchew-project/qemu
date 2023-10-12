from __future__ import print_function
#
# Exercise the register functionality by exhaustively iterating
# through all supported registers on the system.
#
# This is launched via tests/guest-debug/run-test.py but you can also
# call it directly if using it for debugging/introspection:
#
#
#

import gdb
import sys
import xml.etree.ElementTree as ET

initial_vlen = 0
failcount = 0

def report(cond, msg):
    "Report success/fail of test."
    if cond:
        print("PASS: %s" % (msg))
    else:
        print("FAIL: %s" % (msg))
        global failcount
        failcount += 1


def fetch_xml_regmap():
    """
    Iterate through the XML descriptions and validate.

    We check for any duplicate registers and report them. Return a
    reg_map hash containing the names, regnums and initial values of
    all registers.
    """

    total_regs = 0
    reg_map = {}
    frame = gdb.selected_frame()

    # First check the XML descriptions we have sent
    xml = gdb.execute("maint print xml-tdesc", False, True)
    tree = ET.fromstring(xml)
    for f in tree.findall("feature"):
        name = f.attrib["name"]
        regs = f.findall("reg")

        total = len(regs)
        total_regs += total
        base = int(regs[0].attrib["regnum"])
        top = int(regs[-1].attrib["regnum"])

        print(f"feature: {name} has {total} registers from {base} to {top}")

        for r in regs:
            name = r.attrib["name"]
            value = frame.read_register(name).__str__()
            regnum = int(r.attrib["regnum"])
            entry = { "name": name,
                      "initial": value,
                      "regnum": regnum }
            try:
                reg_map[name] = entry
            except KeyError:
                report(False, f"duplicate register {r} vs {reg_map[name]}")

    # Validate we match
    report(total_regs == len(reg_map.keys()),
           f"counted all {total_regs} registers in XML")

    return reg_map

def crosscheck_remote_xml(reg_map):
    """
    Cross-check the list of remote-registers with the XML info.
    """

    remote = gdb.execute("maint print remote-registers", False, True)
    r_regs = remote.split("\n")

    total_regs = len(reg_map.keys())
    total_r_regs = 0

    for r in r_regs:
        fields = r.split()
        # Some of the registers reported here are "pseudo" registers that
        # gdb invents based on actual registers so we need to filter them
        # out.
        if len(fields) == 8:
            r_name = fields[0]
            r_regnum = int(fields[1])

            # check in the XML
            try:
                x_reg = reg_map[r_name]
            except KeyError:
                report(False, "{r_name} not in XML description")
                continue

            x_regnum = x_reg["regnum"]
            if r_regnum != x_regnum:
                report(False, f"{r_name} {r_regnum} == {x_regnum} (xml)")
            else:
                total_r_regs += 1

    report(total_regs == total_r_regs, f"xml-tdesc and remote-registers agree")

def complete_and_diff(reg_map):
    """
    Let the program run to (almost) completion and then iterate
    through all the registers we know about and report which ones have
    changed.
    """
    # Let the program get to the end and we can check what changed
    gdb.Breakpoint("_exit")
    gdb.execute("continue")

    frame = gdb.selected_frame()
    changed = 0

    for e in reg_map.values():
        name = e["name"]
        old_val = e["initial"]

        try:
            new_val = frame.read_register(name).__str__()
        except:
            report(False, f"failed to read {name} at end of run")
            continue

        if new_val != old_val:
            print(f"{name} changes from {old_val} to {new_val}")
            changed += 1

    # as long as something changed we can be confident its working
    report(changed > 0, f"{changed} registers were changed")


def run_test():
    "Run through the tests"

    reg_map = fetch_xml_regmap()

    crosscheck_remote_xml(reg_map)

    complete_and_diff(reg_map)


#
# This runs as the script it sourced (via -x, via run-test.py)
#
try:
    inferior = gdb.selected_inferior()
    arch = inferior.architecture()
    print("ATTACHED: %s" % arch.name())
except (gdb.error, AttributeError):
    print("SKIPPING (not connected)", file=sys.stderr)
    exit(0)

if gdb.parse_and_eval('$pc') == 0:
    print("SKIP: PC not set")
    exit(0)

try:
    run_test()
except (gdb.error):
    print ("GDB Exception: %s" % (sys.exc_info()[0]))
    failcount += 1
    pass

print("All tests complete: %d failures" % failcount)
exit(failcount)
