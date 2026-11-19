#!/usr/bin/env python3
#
# Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
#
# SPDX-License-Identifier: GPL-2.0-or-later

import threading
import unittest

from qemu_test import QemuSystemTest, Asset
from qemu_test.cmd import wait_for_console_pattern


class ArchTestsUart(QemuSystemTest):
    """
    Hexagon architecture verification tests

    These are bare-metal tests from hexagon-arch-tests that exercise
    system functionality.

    Tests output results via UART.
    """
    timeout = 180

    ASSET_TARBALL = Asset(
        "https://github.com/qualcomm/qemu-hexagon-testing/releases/"
        "download/v0.3.1/arch_tests_uart.tar.gz",
        "1baba59ad29c24a3d38043cc9f6846fd"
        "fd3f88e8b67501c13722916c6c744c3c",
    )
    ASSET_SEMIHOSTING_TARBALL = Asset(
        "https://github.com/qualcomm/qemu-hexagon-testing/releases/"
        "download/v0.3.1/arch_tests_semihosting.tar.gz",
        "37b460146e7a2a6898ba6454dfea7e1c"
        "64053b31e8e3acf7c97e7e82209b6bda",
    )

    def launch_and_wait_for_pass(self) -> None:
        """
        Run the guest until it prints PASS on the console.

        Start the guest paused and read the console while resuming it, so
        that a guest blocked on a full console socket cannot deadlock QMP.
        """
        self.vm.add_args("-S")
        self.vm.launch()
        resume = threading.Thread(target=self.vm.cmd, args=("cont",),
                                  daemon=True)
        resume.start()
        try:
            wait_for_console_pattern(self, "PASS")
        finally:
            resume.join(timeout=30)

    def run_uart_test(self, test_name: str,
                      machine: str = "virt") -> None:
        """
        Run an arch test binary and verify PASS via UART console output.
        """
        self.set_machine(machine)
        self.archive_extract(self.ASSET_TARBALL)
        target_bin = self.scratch_file('arch_tests_uart_package',
                                      'bin', test_name)
        self.vm.set_console()
        self.set_vm_arg("-display", "none")
        self.set_vm_arg("-kernel", target_bin)
        self.launch_and_wait_for_pass()

    def run_semihosting_test(self, test_name: str,
                             machine: str = "V81QA_1") -> None:
        self.set_machine(machine)
        self.archive_extract(self.ASSET_SEMIHOSTING_TARBALL)
        target_bin = self.scratch_file('arch_tests_semihosting_package',
                                       'bin', test_name)
        self.vm.set_console(semihosting=True)
        self.set_vm_arg("-display", "none")
        self.set_vm_arg("-kernel", target_bin)
        self.launch_and_wait_for_pass()

    def test_exceptions(self) -> None:
        """Tests exception delivery for trap instructions, privilege
        violations, and verifies SSR cause codes and ELR values.
        """
        self.run_uart_test("test_exceptions")

    def test_guest_mode(self) -> None:
        """Tests guest mode entry/exit via CCR configuration, verifying
        GSR fields, GELR, and guest event vector table dispatch.
        """
        self.run_uart_test("test_guest_mode")

    def test_int_steering(self) -> None:
        """Tests interrupt steering via priority-based routing to
        specific threads using STID priority and iassignw.
        """
        self.run_uart_test("test_int_steering")

    def test_interrupts(self) -> None:
        """Tests interrupt delivery."""
        self.run_uart_test("test_interrupts")

    def test_cache(self) -> None:
        """Tests cache operations: dckill/ickill, l2kill, dczeroa,
        dccleaninva, cache disable/enable, barriers, and dcinva/dccleana.
        """
        self.run_uart_test("test_cache")

    def test_l2vic(self) -> None:
        """Tests the L2VIC interrupt controller: enable readback,
        interrupt type readback, VID capture, and the fast interface.
        """
        self.run_uart_test("test_l2vic")

    def test_sys_regs(self) -> None:
        """Tests system registers."""
        self.run_uart_test("test_sys_regs")

    def test_sys_regs_v81(self) -> None:
        """Tests V81-specific system register masks."""
        self.run_semihosting_test("test_sys_regs")

    def test_cache_v81(self) -> None:
        """Tests cache operations on the V81 machine."""
        self.run_semihosting_test("test_cache")

    def test_exceptions_v81(self) -> None:
        """Tests exception delivery on the V81 machine."""
        self.run_semihosting_test("test_exceptions")

    def test_guest_mode_v81(self) -> None:
        """Tests guest mode on the V81 machine."""
        self.run_semihosting_test("test_guest_mode")

    def test_hvx_context_v81(self) -> None:
        """Tests the HVX coprocessor context on the V81 machine."""
        self.run_semihosting_test("test_hvx_context")

    def test_int_steering_v81(self) -> None:
        """Tests interrupt steering on the V81 machine."""
        self.run_semihosting_test("test_int_steering")

    def test_interrupts_v81(self) -> None:
        """Tests interrupt delivery on the V81 machine."""
        self.run_semihosting_test("test_interrupts")

    @unittest.skip("test_isr_stress in v0.3.1 gives another hardware "
                   "thread a fixed polling budget, so it fails when the "
                   "host delays that vCPU thread")
    def test_isr_stress_v81(self) -> None:
        """Tests interrupt service routine stress on the V81 machine."""
        self.run_semihosting_test("test_isr_stress")

    def test_l2vic_v81(self) -> None:
        """Tests the L2VIC on the V81 machine."""
        self.run_semihosting_test("test_l2vic")

    def test_pagecross_tlbmissx_v81(self) -> None:
        """Tests page-crossing instruction TLB misses on the V81 machine."""
        self.run_semihosting_test("test_pagecross_tlbmissx")

    def test_pmu_v81(self) -> None:
        """Tests the performance counters on the V81 machine."""
        self.run_semihosting_test("test_pmu")

    def test_threads_v81(self) -> None:
        """Tests hardware thread management on the V81 machine."""
        self.run_semihosting_test("test_threads")

    def test_timer_v81(self) -> None:
        """Tests the system timer on the V81 machine."""
        self.run_semihosting_test("test_timer")

    def test_tlb_mmu_v81(self) -> None:
        """Tests TLB/MMU operations on the V81 machine."""
        self.run_semihosting_test("test_tlb_mmu")

    def test_user_mode_v81(self) -> None:
        """Tests user mode transitions on the V81 machine."""
        self.run_semihosting_test("test_user_mode")

    def test_threads(self) -> None:
        """Tests hardware thread management: start/stop, MODECTL state,
        per-thread HTID, shared memory, wait/resume, STID priority, and
        SCHEDCFG/BESTWAIT readback.
        """
        self.run_uart_test("test_threads")

    def test_tlb_mmu(self) -> None:
        """Tests TLB/MMU operations: write/read/probe/invalidate,
        global entries, multiple entries, overwrite, ASID matching,
        and permission checks.
        """
        self.run_uart_test("test_tlb_mmu")

    @unittest.skip("test_isr_stress in v0.3.1 gives another hardware "
                   "thread a fixed polling budget, so it fails when the "
                   "host delays that vCPU thread")
    def test_isr_stress(self) -> None:
        """Tests interrupt service routine stress: bursts of interrupts,
        simultaneous software interrupts, rapid refire, nesting, and
        interrupts while threads start, spin, and wait.
        """
        self.run_uart_test("test_isr_stress")

    def test_pagecross_tlbmissx(self) -> None:
        """Tests instruction TLB misses for packets that cross a page
        boundary: the next page missing, both pages missing, and the
        default handler.
        """
        self.run_uart_test("test_pagecross_tlbmissx")

    def test_pmu(self) -> None:
        """Tests the performance counters: PCYCLE incrementing, its high
        and low halves, enabling and disabling it, and UPCYCLE.
        """
        self.run_uart_test("test_pmu")

    def test_timer(self) -> None:
        """Tests the system timer: QTimer reads, monotonicity, version, and
        using PCYCLE as a timer.
        """
        self.run_uart_test("test_timer")

    def test_user_mode(self) -> None:
        """Tests user mode / privilege transitions: supervisor mode,
        SSR UM/IE/XE/CE/PE bits, and the trap0 user-mode exit handler.
        """
        self.run_uart_test("test_user_mode")

    def test_hvx_context(self) -> None:
        """Tests the HVX coprocessor context: SSR XA context selection
        and isolation, vsplat/store, and the NO_COPROC_ENABLE exception
        raised when SSR:XE is clear.
        """
        self.run_uart_test("test_hvx_context")


if __name__ == "__main__":
    QemuSystemTest.main()
