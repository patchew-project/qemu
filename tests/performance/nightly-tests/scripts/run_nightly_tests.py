#!/usr/bin/env python3

"""
Entry point script for running nightly performance tests on QEMU.

This file is a part of the project "TCG Continuous Benchmarking".

Copyright (C) 2020  Ahmed Karaman <ahmedkhaledkaraman@gmail.com>
Copyright (C) 2020  Aleksandar Markovic <aleksandar.qemu.devel@gmail.com>

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 2 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program. If not, see <https://www.gnu.org/licenses/>.
"""


import datetime
import io
import os
import subprocess
import time
import sys
from send_email import send_email


# Record system hardware information
with open('/proc/cpuinfo', 'r') as cpuinfo:
    for line in cpuinfo:
        if line.startswith('model name'):
            HOST_CPU = line.rstrip('\n').split(':')[1].strip()
            break
with open('/proc/meminfo', 'r') as meminfo:
    for line in meminfo:
        if line.startswith('MemTotal'):
            HOST_MEMORY_KB = int(line.rstrip('\n').split(':')[1].
                                 strip().split(' ')[0])
            HOST_MEMORY = str(round(HOST_MEMORY_KB / (1024 * 1024), 2)) + " GB"
            break

# Find path for the "nightly_tests_core.py" script
NIGHTLY_TESTS_CORE_PATH = os.path.join(
    os.path.dirname(os.path.abspath(__file__)),
    "nightly_tests_core.py")

NIGHTLY_TESTS_ARGUMENTS = sys.argv[1:]

# Start the nightly test
START_EPOCH = time.time()
RUN_NIGHTLY_TESTS = subprocess.run([NIGHTLY_TESTS_CORE_PATH,
                                    *NIGHTLY_TESTS_ARGUMENTS],
                                   stdout=subprocess.PIPE,
                                   stderr=subprocess.PIPE,
                                   check=False)
END_EPOCH = time.time()

# Perform time calculations
EXECUTION_TIME = datetime.timedelta(seconds=END_EPOCH - START_EPOCH)
TEST_DATE = datetime.datetime.utcfromtimestamp(
    START_EPOCH).strftime('%A, %B %-d, %Y')
START_TIME = datetime.datetime.utcfromtimestamp(
    START_EPOCH).strftime('%Y-%m-%d %H:%M:%S')
END_TIME = datetime.datetime.utcfromtimestamp(
    END_EPOCH).strftime('%Y-%m-%d %H:%M:%S')

# Get nightly test status
if RUN_NIGHTLY_TESTS.returncode:
    STATUS = "FAILURE"
else:
    STATUS = "SUCCESS"

# Initialize a StringIO to print all the output into
OUTPUT = io.StringIO()

# Print the nightly test statistical information
print("{:<17}: {}\n{:<17}: {}\n".
      format("Host CPU",
             HOST_CPU,
             "Host Memory",
             HOST_MEMORY), file=OUTPUT)
print("{:<17}: {}\n{:<17}: {}\n{:<17}: {}\n".
      format("Start Time (UTC)",
             START_TIME,
             "End Time (UTC)",
             END_TIME,
             "Execution Time",
             EXECUTION_TIME), file=OUTPUT)
print("{:<17}: {}\n".format("Status", STATUS), file=OUTPUT)

if STATUS == "SUCCESS":
    print("Note:\nChanges denoted by '-----' are less than 0.01%.\n",
          file=OUTPUT)

# Print the nightly test stdout (main output)
print(RUN_NIGHTLY_TESTS.stdout.decode("utf-8"), file=OUTPUT)

# If the nightly test failed, print the stderr (error logs)
if STATUS == "FAILURE":
    print("{}\n{}\n{}".format("-" * 56,
                              " " * 18 + "ERROR LOGS",
                              "-" * 56), file=OUTPUT)
    print(RUN_NIGHTLY_TESTS.stderr.decode("utf-8"), file=OUTPUT)


# Temp file to store the output in case sending the email failed
# with open("temp.txt", "w") as file:
#     file.write(OUTPUT.getvalue())

# Use an HTML message to preserve monospace formatting
HTML_MESSAGE = """\
<html><body><pre>
{body}
</pre></body></html>
""".format(body=OUTPUT.getvalue())
OUTPUT.close()

# Send the nightly test results email to the QEMU mailing list
while True:
    try:
        send_email("[REPORT] Nightly Performance Tests - {}".format(TEST_DATE),
                   ["qemu-devel@nongnu.org"], HTML_MESSAGE)
    except Exception:  # pylint: disable=W0703
        # Wait for a minute then retry sending
        time.sleep(60)
        continue
    else:
        break
