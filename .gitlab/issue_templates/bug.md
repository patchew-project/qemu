<!--
This is the upstream QEMU issue tracker.

Before submitting a bug, please attempt to reproduce your problem using
the latest development version of QEMU obtained from
https://gitlab.com/qemu-project/qemu/.

QEMU generally supports the last two releases advertised via
https://www.qemu.org/. Problems with distro-packaged versions of QEMU
older than this should be reported to the distribution instead.

See https://www.qemu.org/contribute/report-a-bug/ for guidance.
-->

## Host environment
 - Operating system: (Windows 10 21H1, Fedora 34, etc.)
 - OS/kernel version: (For POSIX hosts, use `uname -a`)
 - Architecture: (x86, ARM, s390x, etc.)
 - QEMU flavor: (qemu-system-x86_64, qemu-aarch64, qemu-img, etc.)
 - QEMU version: (e.g. `qemu-system-x86_64 --version`)
 - QEMU command line:
   <!--
   Give the smallest, complete command line that exhibits the problem.

   If you are using libvirt, virsh, or vmm, you can likely find the QEMU
   command line arguments in /var/log/libvirt/qemu/$GUEST.log.
   -->
   ```
   ./qemu-system-x86_64 -M q35 -m 4096 -enable-kvm -hda fedora32.qcow2
   ```

## Emulated/Virtualized environment
 - Operating system: (Windows 10 21H1, Fedora 34, etc.)
 - OS/kernel version: (For POSIX guests, use `uname -a`.)
 - Architecture: (x86, ARM, s390x, etc.)


## Description of problem
<!-- Describe the problem, including any error/crash messages seen. -->

## Steps to reproduce
1.
2.
3.


## Additional information

<!--
Attach logs, stack traces, screenshots, etc. Compress the files if necessary.
If using libvirt, libvirt logs and XML domain information may be relevant.

See https://qemu-project.gitlab.io/qemu/devel/tracing.html on how to
configure additional QEMU logging.
-->

<!--
The line below ensures that proper tags are added to the issue.
Please do not remove it.
-->
/label ~"kind::Bug"
