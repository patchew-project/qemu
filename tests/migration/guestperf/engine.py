from __future__ import print_function
#
# Migration test main engine
#
# Copyright (c) 2016 Red Hat, Inc.
#
# This library is free software; you can redistribute it and/or
# modify it under the terms of the GNU Lesser General Public
# License as published by the Free Software Foundation; either
# version 2 of the License, or (at your option) any later version.
#
# This library is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
# Lesser General Public License for more details.
#
# You should have received a copy of the GNU Lesser General Public
# License along with this library; if not, see <http://www.gnu.org/licenses/>.
#


import os
import re
import sys
import time

from guestperf.progress import Progress, ProgressStats
from guestperf.report import Report
from guestperf.timings import TimingRecord, Timings

THIS_DIR = os.path.dirname(os.path.abspath(__file__))
TOP_DIR = os.path.dirname(os.path.dirname(os.path.dirname(THIS_DIR)))
PYTHON_MODULE_PATH = os.path.join(TOP_DIR, 'python')
sys.path.append(PYTHON_MODULE_PATH)

import qemu


class Engine(object):

    def __init__(self, binary, dst_host, kernel, initrd, transport="tcp",
                 sleep=15, verbose=False, debug=False):

        self._binary = binary # Path to QEMU binary
        self._dst_host = dst_host # Hostname of target host
        self._kernel = kernel # Path to kernel image
        self._initrd = initrd # Path to stress initrd
        self._transport = transport # 'unix' or 'tcp' or 'rdma'
        self._sleep = sleep
        self._verbose = verbose
        self._debug = debug

        if debug:
            self._verbose = debug

    def _vcpu_timing(self, pid, tid_list):
        records = []
        now = time.time()

        jiffies_per_sec = os.sysconf(os.sysconf_names['SC_CLK_TCK'])
        for tid in tid_list:
            statfile = "/proc/%d/task/%d/stat" % (pid, tid)
            with open(statfile, "r") as fh:
                stat = fh.readline()
                fields = stat.split(" ")
                stime = int(fields[13])
                utime = int(fields[14])
                records.append(TimingRecord(tid, now, 1000 * (stime + utime) / jiffies_per_sec))
        return records

    def _cpu_timing(self, pid):
        records = []
        now = time.time()

        jiffies_per_sec = os.sysconf(os.sysconf_names['SC_CLK_TCK'])
        statfile = "/proc/%d/stat" % pid
        with open(statfile, "r") as fh:
            stat = fh.readline()
            fields = stat.split(" ")
            stime = int(fields[13])
            utime = int(fields[14])
            return TimingRecord(pid, now, 1000 * (stime + utime) / jiffies_per_sec)

    def _migrate_progress(self, vm):
        info = vm.command("query-migrate")

        if "ram" not in info:
            info["ram"] = {}

        return Progress(
            info.get("status", "active"),
            ProgressStats(
                info["ram"].get("transferred", 0),
                info["ram"].get("remaining", 0),
                info["ram"].get("total", 0),
                info["ram"].get("duplicate", 0),
                info["ram"].get("skipped", 0),
                info["ram"].get("normal", 0),
                info["ram"].get("normal-bytes", 0),
                info["ram"].get("dirty-pages-rate", 0),
                info["ram"].get("mbps", 0),
                info["ram"].get("dirty-sync-count", 0)
            ),
            time.time(),
            info.get("total-time", 0),
            info.get("downtime", 0),
            info.get("expected-downtime", 0),
            info.get("setup-time", 0),
            info.get("x-cpu-throttle-percentage", 0),
        )

    def _migrate(self, hardware, scenario, src, dst, connect_uri):
        src_qemu_time = []
        src_vcpu_time = []
        src_pid = src.get_pid()

        vcpus = src.command("query-cpus")
        src_threads = []
        for vcpu in vcpus:
            src_threads.append(vcpu["thread_id"])

        # XXX how to get dst timings on remote host ?

        if self._verbose:
            print("Sleeping %d seconds for initial guest workload run" % self._sleep)
        sleep_secs = self._sleep
        while sleep_secs > 1:
            src_qemu_time.append(self._cpu_timing(src_pid))
            src_vcpu_time.extend(self._vcpu_timing(src_pid, src_threads))
            time.sleep(1)
            sleep_secs -= 1

        if self._verbose:
            print("Starting migration")
        if scenario._auto_converge:
            resp = src.command("migrate-set-capabilities",
                               capabilities = [
                                   { "capability": "auto-converge",
                                     "state": True }
                               ])
            resp = src.command("migrate-set-parameters",
                               x_cpu_throttle_increment=scenario._auto_converge_step)

        if scenario._post_copy:
            resp = src.command("migrate-set-capabilities",
                               capabilities = [
                                   { "capability": "postcopy-ram",
                                     "state": True }
                               ])
            resp = dst.command("migrate-set-capabilities",
                               capabilities = [
                                   { "capability": "postcopy-ram",
                                     "state": True }
                               ])

        resp = src.command("migrate_set_speed",
                           value=scenario._bandwidth * 1024 * 1024)

        resp = src.command("migrate_set_downtime",
                           value=scenario._downtime / 1024.0)

        if scenario._compression_mt:
            resp = src.command("migrate-set-capabilities",
                               capabilities = [
                                   { "capability": "compress",
                                     "state": True }
                               ])
            resp = src.command("migrate-set-parameters",
                               compress_threads=scenario._compression_mt_threads)
            resp = dst.command("migrate-set-capabilities",
                               capabilities = [
                                   { "capability": "compress",
                                     "state": True }
                               ])
            resp = dst.command("migrate-set-parameters",
                               decompress_threads=scenario._compression_mt_threads)

        if scenario._compression_xbzrle:
            resp = src.command("migrate-set-capabilities",
                               capabilities = [
                                   { "capability": "xbzrle",
                                     "state": True }
                               ])
            resp = dst.command("migrate-set-capabilities",
                               capabilities = [
                                   { "capability": "xbzrle",
                                     "state": True }
                               ])
            resp = src.command("migrate-set-cache-size",
                               value=(hardware._mem * 1024 * 1024 * 1024 / 100 *
                                      scenario._compression_xbzrle_cache))

        resp = src.command("migrate", uri=connect_uri)

        post_copy = False
        paused = False

        progress_history = []

        start = time.time()
        loop = 0
        while True:
            loop = loop + 1
            time.sleep(0.05)

            progress = self._migrate_progress(src)
            if (loop % 20) == 0:
                src_qemu_time.append(self._cpu_timing(src_pid))
                src_vcpu_time.extend(self._vcpu_timing(src_pid, src_threads))

            if (len(progress_history) == 0 or
                (progress_history[-1]._ram._iterations <
                 progress._ram._iterations)):
                progress_history.append(progress)

            if progress._status in ("completed", "failed", "cancelled"):
                if progress._status == "completed" and paused:
                    dst.command("cont")
                if progress_history[-1] != progress:
                    progress_history.append(progress)

                if progress._status == "completed":
                    if self._verbose:
                        print("Sleeping %d seconds for final guest workload run" % self._sleep)
                    sleep_secs = self._sleep
                    while sleep_secs > 1:
                        time.sleep(1)
                        src_qemu_time.append(self._cpu_timing(src_pid))
                        src_vcpu_time.extend(self._vcpu_timing(src_pid, src_threads))
                        sleep_secs -= 1

                return [progress_history, src_qemu_time, src_vcpu_time]

            if self._verbose and (loop % 20) == 0:
                print("Iter %d: remain %5dMB of %5dMB (total %5dMB @ %5dMb/sec)" % (
                    progress._ram._iterations,
                    progress._ram._remaining_bytes / (1024 * 1024),
                    progress._ram._total_bytes / (1024 * 1024),
                    progress._ram._transferred_bytes / (1024 * 1024),
                    progress._ram._transfer_rate_mbs,
                ))

            if progress._ram._iterations > scenario._max_iters:
                if self._verbose:
                    print("No completion after %d iterations over RAM" % scenario._max_iters)
                src.command("migrate_cancel")
                continue

            if time.time() > (start + scenario._max_time):
                if self._verbose:
                    print("No completion after %d seconds" % scenario._max_time)
                src.command("migrate_cancel")
                continue

            if (scenario._post_copy and
                progress._ram._iterations >= scenario._post_copy_iters and
                not post_copy):
                if self._verbose:
                    print("Switching to post-copy after %d iterations" % scenario._post_copy_iters)
                resp = src.command("migrate-start-postcopy")
                post_copy = True

            if (scenario._pause and
                progress._ram._iterations >= scenario._pause_iters and
                not paused):
                if self._verbose:
                    print("Pausing VM after %d iterations" % scenario._pause_iters)
                resp = src.command("stop")
                paused = True

    def _get_common_args(self, hardware, tunnelled=False):
        args = [
            "noapic",
            "edd=off",
            "printk.time=1",
            "noreplace-smp",
            "cgroup_disable=memory",
            "pci=noearly",
            "console=ttyS0",
        ]
        if self._debug:
            args.append("debug")
        else:
            args.append("quiet")

        args.append("ramsize=%s" % hardware._mem)

        cmdline = " ".join(args)
        if tunnelled:
            cmdline = "'" + cmdline + "'"

        argv = [
            "-machine", "accel=kvm",
            "-cpu", "host",
            "-kernel", self._kernel,
            "-initrd", self._initrd,
            "-append", cmdline,
            "-chardev", "stdio,id=cdev0",
            "-device", "isa-serial,chardev=cdev0",
            "-m", str((hardware._mem * 1024) + 512),
            "-smp", str(hardware._cpus),
        ]

        if self._debug:
            argv.extend(["-device", "sga"])

        if hardware._prealloc_pages:
            argv_source += ["-mem-path", "/dev/shm",
                            "-mem-prealloc"]
        if hardware._locked_pages:
            argv_source += ["-realtime", "mlock=on"]
        if hardware._huge_pages:
            pass

        return argv

    def _get_src_args(self, hardware):
        return self._get_common_args(hardware)

    def _get_dst_args(self, hardware, uri):
        tunnelled = False
        if self._dst_host != "localhost":
            tunnelled = True
        argv = self._get_common_args(hardware, tunnelled)
        return argv + ["-incoming", uri]

    @staticmethod
    def _get_common_wrapper(cpu_bind, mem_bind):
        wrapper = []
        if len(cpu_bind) > 0 or len(mem_bind) > 0:
            wrapper.append("numactl")
            if cpu_bind:
                wrapper.append("--physcpubind=%s" % ",".join(cpu_bind))
            if mem_bind:
                wrapper.append("--membind=%s" % ",".join(mem_bind))

        return wrapper

    def _get_src_wrapper(self, hardware):
        return self._get_common_wrapper(hardware._src_cpu_bind, hardware._src_mem_bind)

    def _get_dst_wrapper(self, hardware):
        wrapper = self._get_common_wrapper(hardware._dst_cpu_bind, hardware._dst_mem_bind)
        if self._dst_host != "localhost":
            return ["ssh",
                    "-R", "9001:localhost:9001",
                    self._dst_host] + wrapper
        else:
            return wrapper

    def _get_timings(self, vm):
        log = vm.get_log()
        if not log:
            return []
        if self._debug:
            print(log)

        regex = r"[^\s]+\s\((\d+)\):\sINFO:\s(\d+)ms\scopied\s\d+\sGB\sin\s(\d+)ms"
        matcher = re.compile(regex)
        records = []
        for line in log.split("\n"):
            match = matcher.match(line)
            if match:
                records.append(TimingRecord(int(match.group(1)),
                                            int(match.group(2)) / 1000.0,
                                            int(match.group(3))))
        return records

    def run(self, hardware, scenario, result_dir=os.getcwd()):
        abs_result_dir = os.path.join(result_dir, scenario._name)

        if self._transport == "tcp":
            uri = "tcp:%s:9000" % self._dst_host
        elif self._transport == "rdma":
            uri = "rdma:%s:9000" % self._dst_host
        elif self._transport == "unix":
            if self._dst_host != "localhost":
                raise Exception("Running use unix migration transport for non-local host")
            uri = "unix:/var/tmp/qemu-migrate-%d.migrate" % os.getpid()
            try:
                os.remove(uri[5:])
                os.remove(monaddr)
            except:
                pass

        if self._dst_host != "localhost":
            dstmonaddr = ("localhost", 9001)
        else:
            dstmonaddr = "/var/tmp/qemu-dst-%d-monitor.sock" % os.getpid()
        srcmonaddr = "/var/tmp/qemu-src-%d-monitor.sock" % os.getpid()

        src = qemu.QEMUMachine(self._binary,
                               args=self._get_src_args(hardware),
                               wrapper=self._get_src_wrapper(hardware),
                               name="qemu-src-%d" % os.getpid(),
                               monitor_address=srcmonaddr)

        dst = qemu.QEMUMachine(self._binary,
                               args=self._get_dst_args(hardware, uri),
                               wrapper=self._get_dst_wrapper(hardware),
                               name="qemu-dst-%d" % os.getpid(),
                               monitor_address=dstmonaddr)

        try:
            src.launch()
            dst.launch()

            ret = self._migrate(hardware, scenario, src, dst, uri)
            progress_history = ret[0]
            qemu_timings = ret[1]
            vcpu_timings = ret[2]
            if uri[0:5] == "unix:":
                os.remove(uri[5:])
            if self._verbose:
                print("Finished migration")

            src.shutdown()
            dst.shutdown()

            return Report(hardware, scenario, progress_history,
                          Timings(self._get_timings(src) + self._get_timings(dst)),
                          Timings(qemu_timings),
                          Timings(vcpu_timings),
                          self._binary, self._dst_host, self._kernel,
                          self._initrd, self._transport, self._sleep)
        except Exception as e:
            if self._debug:
                print("Failed: %s" % str(e))
            try:
                src.shutdown()
            except:
                pass
            try:
                dst.shutdown()
            except:
                pass

            if self._debug:
                print(src.get_log())
                print(dst.get_log())
            raise

