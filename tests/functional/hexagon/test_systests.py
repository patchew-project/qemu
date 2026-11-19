#!/usr/bin/env python3
#
# Copyright(c) Qualcomm Innovation Center, Inc. All Rights Reserved.
# SPDX-License-Identifier: GPL-2.0-or-later

import os
import re
import time
import unittest

from qemu_test import QemuSystemTest, Asset, wait_for_console_pattern


_TARBALL_BIN_PATH = (
    "systests_standalone_package",
    "StandaloneSysTests_6.7.0.0_v68",
    "bin",
)

class SysTestsStandaloneTests(QemuSystemTest):
    SYSTEST_TIMEOUT_SEC = 30

    ASSET_TARBALL = Asset(
        "https://github.com/qualcomm/qemu-hexagon-testing/releases/"
        "download/v0.3.1/systests_standalone.tar.gz",
        "40163fa94d67a63e5ef645fc83682e4e"
        "b56e1d79a9068614055e74f35d4cf70d",
    )

    def setUp(self):
        super().setUp()
        self.archive_extract(self.ASSET_TARBALL)

    def binary(self, name):
        path = self.scratch_file(*_TARBALL_BIN_PATH, name)
        self.assertTrue(os.path.exists(path))
        return path

    def run_exit_zero(self, binary_name, *extra_args, machine="sim",
                      timeout=60.0):
        self.set_machine(machine)
        self.set_vm_arg("-display", "none")
        self.set_vm_arg("-kernel", self.binary(binary_name))
        for flag, value in zip(extra_args[::2], extra_args[1::2]):
            self.set_vm_arg(flag, value)
        self.vm.launch()
        self.vm.wait(timeout=timeout)
        self.assertEqual(self.vm.exitcode(), 0,
                         f"Test {binary_name} exited with "
                         f"code {self.vm.exitcode()}, expected 0")

    def run_console_pattern(self, binary_name, pattern, *extra_args,
                            machine="sim"):
        self.set_machine(machine)
        self.set_vm_arg("-display", "none")
        self.set_vm_arg("-kernel", self.binary(binary_name))
        for flag, value in zip(extra_args[::2], extra_args[1::2]):
            self.set_vm_arg(flag, value)
        self.vm.set_console(semihosting=True)
        self.vm.launch()
        try:
            wait_for_console_pattern(self, pattern)
        finally:
            self.vm.kill()

    def run_expect_crash(self, binary_name, log_pattern, *extra_args,
                         machine="sim"):
        """Run a test that is meant to crash.

        The standalone CRT exits with 255 from its unhandled-exception
        handler, which also reports the cause in the log.
        """
        self.set_machine(machine)
        self.set_vm_arg("-display", "none")
        self.set_vm_arg("-kernel", self.binary(binary_name))
        for flag, value in zip(extra_args[::2], extra_args[1::2]):
            self.set_vm_arg(flag, value)
        self.vm.launch()
        self.vm.wait(timeout=60.0)
        self.assertEqual(self.vm.exitcode(), 255,
                         f"{binary_name} exited with {self.vm.exitcode()}, "
                         "expected 255")
        log = self.vm.get_log() or ""
        self.assertTrue(re.search(log_pattern, log),
                        f"{log_pattern!r} not in the log of {binary_name}:"
                        f"\n{log}")

    def wait_for_log_pattern(self, pattern, count=1, timeout=30.0):
        """Wait until the log of the running VM has @pattern @count times."""
        deadline = time.monotonic() + timeout
        log_path = self.vm._qemu_log_path
        while time.monotonic() < deadline:
            with open(log_path, "r", encoding="utf-8",
                      errors="replace") as f:
                if f.read().count(pattern) >= count:
                    return
            time.sleep(0.1)
        self.fail(f"Timed out waiting for {count} of {pattern!r} in the log")

    def run_hang_log_pattern(self, binary_name, pattern):
        """Run a test that is meant to hang, and expect @pattern logged."""
        self.set_machine("sim")
        self.set_vm_arg("-display", "none")
        self.set_vm_arg("-kernel", self.binary(binary_name))
        self.set_vm_arg("-d", "guest_errors")
        self.vm.launch()
        try:
            self.wait_for_log_pattern(pattern)
        finally:
            self.vm.kill()

    def test_fopen(self):
        """fopen reads a file passed via --append and verifies its contents."""
        import tempfile
        # The fopen binary has a short cmdline buffer; use a short path.
        dummy = os.path.join(tempfile.gettempdir(), "qemu_fopen_test.so")
        with open(dummy, "w", encoding="utf-8") as f:
            f.write("valid\n")
        self.run_exit_zero("fopen", "-append", dummy)

    def test_ftrunc(self):
        """ftrunc truncates _testfile_ftrunc from 6 bytes to 1 byte."""
        ftrunc_path = self.scratch_file("_testfile_ftrunc")
        with open(ftrunc_path, "w", encoding="utf-8") as f:
            f.write("valid\n")
        # Sleep 1 s so mtime change is observable
        time.sleep(1)
        self.run_exit_zero("ftrunc", "-append", ftrunc_path)
        self.assertEqual(os.path.getsize(ftrunc_path), 1,
                         "_testfile_ftrunc should be 1 byte after ftrunc")

    def test_float_excp(self):
        """Unmasked floating point exceptions are taken after the packet
        that caused them commits."""
        self.run_exit_zero("float_excp")

    def test_framelimit(self):
        """allocframe past FRAMELIMIT in user mode raises the stack limit
        exception at the allocframe packet."""
        self.run_exit_zero("framelimit")

    def test_mmu_cacheops(self):
        """Cache operations raise TLB miss and privilege exceptions."""
        self.run_exit_zero("mmu_cacheops")

    def test_llsc_on_excp(self):
        """An exception between a load-locked and the store-conditional
        clears the reservation."""
        self.run_exit_zero("llsc_on_excp")

    def test_access(self):
        """access checks R_OK|W_OK on _testfile_access."""
        testfile = self.scratch_file("_testfile_access")
        with open(testfile, "w", encoding="utf-8") as f:
            f.write("valid\n")
        self.run_exit_zero("access", "-append", testfile)

    def test_semihost(self):
        self.run_console_pattern("semihost", "PASS", "-append", "arg1", "arg2")

    def test_swi_touch_memory(self):
        """A semihosting read restarts after a fault on its second page."""
        self.run_exit_zero("swi_touch_memory")

    def test_invalid_hmx(self):
        self.run_exit_zero("invalid_hmx")

    def test_dtg_interrupt(self):
        self.run_exit_zero("dtg_interrupt")

    def test_bestwait(self):
        self.run_exit_zero("bestwait")

    def test_mmu_multi_tlb(self):
        self.run_exit_zero("mmu_multi_tlb")

    def test_timer_reg(self):
        self.run_exit_zero("timer_reg")

    def test_hvx_multi(self):
        self.run_exit_zero("hvx-multi")

    def test_standalone_vec(self):
        self.run_exit_zero("standalone_vec")

    @unittest.skip("lock_verify has timing defects before v0.2.22 "
                   "and in v0.3.1")
    def test_lock_verify(self):
        self.run_exit_zero("lock_verify")

    def test_single_step(self):
        self.run_exit_zero("single_step")

    def test_start(self):
        """The start instruction resets the threads it names but not the
        thread executing it."""
        self.run_exit_zero("start")

    def test_test_thread(self):
        """Eight threads start, record their ids, and exit."""
        self.run_exit_zero("test-thread", "-smp", "cpus=8")

    def test_test_thread_single_threaded_tcg(self):
        """test_thread with the vCPUs sharing one host thread."""
        self.run_exit_zero("test-thread", "-smp", "cpus=8",
                           "-accel", "tcg,thread=single")

    def test_k0locklock(self):
        """Locking k0 twice in a row hangs the thread, and is reported."""
        self.run_hang_log_pattern("k0locklock", "Double k0lock at PC")

    def test_tlblocklock(self):
        """Locking the TLB twice in a row hangs the thread, and is reported."""
        self.run_hang_log_pattern("tlblocklock", "Double tlblock at PC")

    def test_inf_loop(self):
        """Four threads start, spin forever, and start again after a
        system reset."""
        self.set_machine("sim")
        self.set_vm_arg("-display", "none")
        self.set_vm_arg("-kernel", self.binary("inf-loop"))
        self.vm.launch()
        try:
            for i in range(4):
                self.wait_for_log_pattern(f"Starting thread {i}")
            self.vm.qmp("system_reset")
            for i in range(4):
                self.wait_for_log_pattern(f"Starting thread {i}", count=2)
        finally:
            self.vm.kill()

    def test_unaligned(self):
        """A misaligned load raises the misaligned load exception."""
        self.run_expect_crash("unaligned", "Misaligned Load")

    def test_hvx_nocoproc(self):
        """An HVX instruction with SSR:XE clear raises the coprocessor
        exception."""
        self.run_expect_crash("hvx_nocoproc",
                              "Illegal Execution of Coprocessor Instruction")

    def test_neg_no_hmx(self):
        """HMX instructions are invalid on a machine without HMX."""
        self.run_expect_crash("neg-no-hmx", "Invalid packet",
                              machine="V68N_1024")

    def test_badva(self):
        """Dual memory operations and TLB exceptions record the right BADVA."""
        self.run_exit_zero("badva")

    def test_checkforpriv(self):
        """Privileged instructions raise a privilege exception in user mode."""
        self.run_exit_zero("checkforpriv")

    def test_ciad_siad(self):
        """The ciad and siad instructions clear and set interrupt state."""
        self.run_exit_zero("ciad-siad")

    def test_fastint(self):
        """The fast L2VIC interface handles a large number of interrupts."""
        self.run_exit_zero("fastint")

    def test_fastl2vic(self):
        """The fast L2VIC interface sets and clears interrupt enables."""
        self.run_exit_zero("fastl2vic")

    def test_getcwd(self):
        """The getcwd semihosting call fills a buffer with a valid path."""
        self.run_exit_zero("getcwd")

    def test_gregs(self):
        """Guest registers g0-g31 keep or reject written values."""
        self.run_exit_zero("gregs")

    def test_hvx_64b(self):
        """HVX 64-byte mode accesses raise the expected exceptions."""
        self.run_exit_zero("hvx_64b")

    def test_hvx_ext(self):
        """HVX extension bits and qfloat operations across revisions."""
        self.run_exit_zero("hvx_ext")

    def test_int_range(self):
        """The L2VIC interrupt range 1-1024 can be enabled and cleared."""
        self.run_exit_zero("int_range")

    def test_invalid_insn_for_rev(self):
        """Instructions invalid for the revision raise an exception."""
        self.run_exit_zero("invalid_insn_for_rev")

    def test_invalid_opcode(self):
        """Invalid opcodes raise an exception."""
        self.run_exit_zero("invalid_opcode")

    def test_k0lock(self):
        """k0lock and k0unlock serialize several threads."""
        self.run_exit_zero("k0lock")

    def test_k0lock_syscfg(self):
        """k0lock interacts correctly with SYSCFG changes."""
        self.run_exit_zero("k0lock-syscfg")

    def test_levelint(self):
        """Level-triggered interrupts are delivered and cleared."""
        self.run_exit_zero("levelint")

    def test_mmu_asids(self):
        """TLB entries with different ASIDs translate independently."""
        self.run_exit_zero("mmu_asids")

    def test_mmu_overlap(self):
        """Overlapping TLB entries are rejected by the TLB write."""
        self.run_exit_zero("mmu_overlap")

    def test_mmu_page_size(self):
        """Every page size from 4KB to 1GB translates data and code."""
        self.run_exit_zero("mmu_page_size")

    def test_mmu_permissions(self):
        """TLB permission bits raise miss and permission exceptions."""
        self.run_exit_zero("mmu_permissions")

    def test_nmi_cross_thread(self):
        """An NMI sent to another thread is delivered there."""
        self.run_exit_zero("nmi_cross_thread")

    def test_pendalot(self):
        """1023 interrupts are pended, taken, and logged."""
        self.run_exit_zero("pendalot")

    def test_pend_wake_wait(self):
        """Worker threads wait while software interrupts cycle."""
        self.run_exit_zero("pend_wake_wait")

    def test_qfloat_test(self):
        """HVX qfloat arithmetic, comparisons, min/max, and conversions."""
        self.run_exit_zero("qfloat_test")

    def test_reg_reads(self):
        """Reading the PMU registers and counting HVX packets."""
        self.run_exit_zero("reg-reads")

    def test_standalone_hw(self):
        """The hardware thread and monitor instructions work."""
        self.run_exit_zero("standalone_hw")

    def test_swi(self):
        """Software interrupts are delivered to the expected threads."""
        self.run_exit_zero("swi")

    def test_swi2(self):
        """Software interrupts under load while masks and priorities change."""
        self.run_exit_zero("swi2")

    def test_swi_wait(self):
        """Software interrupts wake threads in wait mode."""
        self.run_exit_zero("swi_wait")

    def test_sys_atomics(self):
        """Load-linked/store-conditional increments across threads."""
        self.run_exit_zero("sys_atomics")

    def test_thread_scheduling(self):
        """Threads are scheduled and make progress."""
        self.run_exit_zero("thread_scheduling")

    def test_tlblock(self):
        """tlblock and tlbunlock serialize several threads."""
        self.run_exit_zero("tlblock")

    def test_vid_reg(self):
        """The VID register accepts valid values and rejects invalid ones."""
        self.run_exit_zero("vid_reg")

    def test_vtcm_error(self):
        """VTCM accesses outside the window raise an error."""
        self.run_exit_zero("vtcm_error")

    def test_rev(self):
        """The revision register reports the V81 of the machine."""
        self.run_console_pattern("rev", "0x81", machine="V81QA_1")

if __name__ == "__main__":
    QemuSystemTest.main()
