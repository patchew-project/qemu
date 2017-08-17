#!/usr/bin/env python
#
# VM testing base class
#
# Copyright (C) 2017 Red Hat Inc.
#
# Authors:
#  Fam Zheng <famz@redhat.com>
#
# This work is licensed under the terms of the GNU GPL, version 2.  See
# the COPYING file in the top-level directory.
#

import os
import sys
import logging
import time
import datetime
sys.path.append(os.path.join(os.path.dirname(__file__), "..", "..", "scripts"))
from qemu import QEMUMachine
import subprocess
import hashlib
import argparse
import atexit
import tempfile
import shutil

SSH_KEY = """\
-----BEGIN RSA PRIVATE KEY-----
MIIEowIBAAKCAQEAopAuOlmLV6LVHdFBj8/eeOwI9CqguIJPp7eAQSZvOiB4Ag/R
coEhl/RBbrV5Yc/SmSD4PTpJO/iM10RwliNjDb4a3I8q3sykRJu9c9PI/YsH8WN9
+NH2NjKPtJIcKTu287IM5JYxyB6nDoOzILbTyJ1TDR/xH6qYEfBAyiblggdjcvhA
RTf93QIn39F/xLypXvT1K2O9BJEsnJ8lEUvB2UXhKo/JTfSeZF8wPBeowaP9EONk
7b+nuJOWHGg68Ji6wVi62tjwl2Szch6lxIhZBpnV7QNRKMfYHP6eIyF4pusazzZq
Telsq6xI2ghecWLzb/MF5A+rklsGx2FNuJSAJwIDAQABAoIBAHHi4o/8VZNivz0x
cWXn8erzKV6tUoWQvW85Lj/2RiwJvSlsnYZDkx5af1CpEE2HA/pFT8PNRqsd+MWC
7AEy710cVsM4BYerBFYQaYxwzblaoojo88LSjVPw3h5Z0iLM8+IMVd36nwuc9dpE
R8TecMZ1+U4Tl6BgqkK+9xToZRdPKdjS8L5MoFhGN+xY0vRbbJbGaV9Q0IHxLBkB
rEBV7T1mUynneCHRUQlJQEwJmKpT8MH3IjsUXlG5YvnuuvcQJSNTaW2iDLxuOKp8
cxW8+qL88zpb1D5dppoIu6rlrugN0azSq70ruFJQPc/A8GQrDKoGgRQiagxNY3u+
vHZzXlECgYEA0dKO3gfkSxsDBb94sQwskMScqLhcKhztEa8kPxTx6Yqh+x8/scx3
XhJyOt669P8U1v8a/2Al+s81oZzzfQSzO1Q7gEwSrgBcRMSIoRBUw9uYcy02ngb/
j/ng3DGivfJztjjiSJwb46FHkJ2JR8mF2UisC6UMXk3NgFY/3vWQx78CgYEAxlcG
T3hfSWSmTgKRczMJuHQOX9ULfTBIqwP5VqkkkiavzigGRirzb5lgnmuTSPTpF0LB
XVPjR2M4q+7gzP0Dca3pocrvLEoxjwIKnCbYKnyyvnUoE9qHv4Kr+vDbgWpa2LXG
JbLmE7tgTCIp20jOPPT4xuDvlbzQZBJ5qCQSoZkCgYEAgrotSSihlCnAOFSTXbu4
CHp3IKe8xIBBNENq0eK61kcJpOxTQvOha3sSsJsU4JAM6+cFaxb8kseHIqonCj1j
bhOM/uJmwQJ4el/4wGDsbxriYOBKpyq1D38gGhDS1IW6kk3erl6VAb36WJ/OaGum
eTpN9vNeQWM4Jj2WjdNx4QECgYAwTdd6mU1TmZCrJRL5ZG+0nYc2rbMrnQvFoqUi
BvWiJovggHzur90zy73tNzPaq9Ls2FQxf5G1vCN8NCRJqEEjeYCR59OSDMu/EXc2
CnvQ9SevHOdS1oEDEjcCWZCMFzPi3XpRih1gptzQDe31uuiHjf3cqcGPzTlPdfRt
D8P92QKBgC4UaBvIRwREVJsdZzpIzm224Bpe8LOmA7DeTnjlT0b3lkGiBJ36/Q0p
VhYh/6cjX4/iuIs7gJbGon7B+YPB8scmOi3fj0+nkJAONue1mMfBNkba6qQTc6Y2
5mEKw2/O7/JpND7ucU3OK9plcw/qnrWDgHxl0Iz95+OzUIIagxne
-----END RSA PRIVATE KEY-----
"""
SSH_PUB_KEY = """\
ssh-rsa AAAAB3NzaC1yc2EAAAADAQABAAABAQCikC46WYtXotUd0UGPz9547Aj0KqC4gk+nt4BBJm86IHgCD9FygSGX9EFutXlhz9KZIPg9Okk7+IzXRHCWI2MNvhrcjyrezKREm71z08j9iwfxY3340fY2Mo+0khwpO7bzsgzkljHIHqcOg7MgttPInVMNH/EfqpgR8EDKJuWCB2Ny+EBFN/3dAiff0X/EvKle9PUrY70EkSycnyURS8HZReEqj8lN9J5kXzA8F6jBo/0Q42Ttv6e4k5YcaDrwmLrBWLra2PCXZLNyHqXEiFkGmdXtA1Eox9gc/p4jIXim6xrPNmpN6WyrrEjaCF5xYvNv8wXkD6uSWwbHYU24lIAn qemu-vm-key
"""

class BaseVM(object):
    GUEST_USER = "qemu"
    GUEST_PASS = "qemupass"
    ROOT_PASS = "qemupass"

    # The script to run in the guest that builds QEMU
    BUILD_SCRIPT = ""
    # The guest name, to be overridden by subclasses
    name = "#base"
    def __init__(self, debug=False):
        self._guest = None
        self._tmpdir = tempfile.mkdtemp(prefix="qemu-vm-")
        atexit.register(shutil.rmtree, self._tmpdir)

        self._ssh_key_file = os.path.join(self._tmpdir, "id_rsa")
        open(self._ssh_key_file, "w").write(SSH_KEY)
        subprocess.check_call(["chmod", "600", self._ssh_key_file])

        self._ssh_pub_key_file = os.path.join(self._tmpdir, "id_rsa.pub")
        open(self._ssh_pub_key_file, "w").write(SSH_PUB_KEY)

        self.debug = debug
        self._stderr = sys.stderr
        self._devnull = open("/dev/null", "w")
        if self.debug:
            self._stdout = sys.stdout
        else:
            self._stdout = self._devnull
        self._args = [ \
            "-nodefaults", "-enable-kvm", "-m", "2G",
            "-smp", os.environ.get("J", "4"), "-cpu", "host",
            "-netdev", "user,id=vnet,hostfwd=:0.0.0.0:0-:22",
            "-device", "virtio-net-pci,netdev=vnet",
            "-vnc", ":0,to=20",
            "-serial", "file:%s" % os.path.join(self._tmpdir, "serial.out")]

        self._data_args = []

    def _download_with_cache(self, url, sha256sum=None):
        def check_sha256sum(fname):
            if not sha256sum:
                return True
            checksum = subprocess.check_output(["sha256sum", fname]).split()[0]
            return sha256sum == checksum

        cache_dir = os.path.expanduser("~/.cache/qemu-vm/download")
        if not os.path.exists(cache_dir):
            os.makedirs(cache_dir)
        fname = os.path.join(cache_dir, hashlib.sha1(url).hexdigest())
        if os.path.exists(fname) and check_sha256sum(fname):
            return fname
        logging.debug("Downloading %s to %s...", url, fname)
        subprocess.check_call(["wget", "-c", url, "-O", fname + ".download"],
                              stdout=self._stdout, stderr=self._stderr)
        os.rename(fname + ".download", fname)
        return fname

    def _ssh_do(self, user, cmd, check, interactive=False):
        ssh_cmd = ["ssh", "-q",
                   "-o", "StrictHostKeyChecking=no",
                   "-o", "UserKnownHostsFile=/dev/null",
                   "-o", "ConnectTimeout=1",
                   "-p", self.ssh_port, "-i", self._ssh_key_file]
        if interactive:
            ssh_cmd += ['-t']
        assert not isinstance(cmd, str)
        ssh_cmd += ["%s@127.0.0.1" % user] + list(cmd)
        logging.debug("ssh_cmd: %s", " ".join(ssh_cmd))
        r = subprocess.call(ssh_cmd,
                            stdin=sys.stdin if interactive else self._devnull,
                            stdout=sys.stdout if interactive else self._stdout,
                            stderr=sys.stderr if interactive else self._stderr)
        if check and r != 0:
            raise Exception("SSH command failed: %s" % cmd)
        return r

    def ssh(self, *cmd):
        return self._ssh_do(self.GUEST_USER, cmd, False)

    def ssh_interactive(self, *cmd):
        return self._ssh_do(self.GUEST_USER, cmd, False, True)

    def ssh_root(self, *cmd):
        return self._ssh_do("root", cmd, False)

    def ssh_check(self, *cmd):
        self._ssh_do(self.GUEST_USER, cmd, True)

    def ssh_root_check(self, *cmd):
        self._ssh_do("root", cmd, True)

    def build_image(self, img):
        raise NotImplementedError

    def add_source_dir(self, data_dir):
        name = "data-" + hashlib.sha1(data_dir).hexdigest()[:5]
        tarfile = os.path.join(self._tmpdir, name + ".tar")
        logging.debug("Creating archive %s for data dir: %s", tarfile, data_dir)
        subprocess.check_call(["tar", "--exclude-vcs",
                               "--exclude=tests/vm/*.img",
                               "--exclude=tests/vm/*.img.*",
                               "--exclude=*.d",
                               "--exclude=*.o",
                               "--exclude=docker-src.*",
                               "-cf", tarfile, '.'], cwd=data_dir,
                              stdin=self._devnull, stdout=self._stdout)
        self._data_args += ["-drive",
                            "file=%s,if=none,id=%s,cache=writeback,format=raw" % \
                                    (tarfile, name),
                            "-device",
                            "virtio-blk,drive=%s,serial=%s,bootindex=1" % (name, name)]

    def boot(self, img, extra_args=[]):
        args = self._args + [
            "-drive", "file=%s,if=none,id=drive0,cache=writeback" % img,
            "-device", "virtio-blk,drive=drive0,bootindex=0"]
        args += self._data_args + extra_args
        logging.debug("QEMU args: %s", " ".join(args))
        guest = QEMUMachine(binary=os.environ.get("QEMU", "qemu-system-x86_64"),
                            args=args)
        guest._vga = "std"
        guest.launch()
        atexit.register(self.shutdown)
        self._guest = guest
        usernet_info = guest.qmp("human-monitor-command",
                                 command_line="info usernet")
        self.ssh_port = None
        for l in usernet_info["return"].splitlines():
            fields = l.split()
            if "TCP[HOST_FORWARD]" in fields and "22" in fields:
                self.ssh_port = l.split()[3]
        if not self.ssh_port:
            raise Exception("Cannot find ssh port from 'info usernet':\n%s" % \
                            usernet_info)

    def wait_ssh(self, seconds=120):
        guest_remote = self.GUEST_USER + "@127.0.0.1"
        starttime = datetime.datetime.now()
        guest_up = False
        while (datetime.datetime.now() - starttime).total_seconds() < seconds:
            if self.ssh("exit 0") == 0:
                guest_up = True
                break
            time.sleep(1)
        if not guest_up:
            logging.error("Timeout while waiting for guest to boot up")
            return 2

    def shutdown(self):
        self._guest.shutdown()

    def wait(self):
        self._guest.wait()

    def qmp(self, *args, **kwargs):
        return self._guest.qmp(*args, **kwargs)

def parse_args(vm_name):
    parser = argparse.ArgumentParser()
    parser.add_argument("--debug", "-D", action="store_true",
                        help="enable debug output")
    parser.add_argument("--image", "-i", default="%s.img" % vm_name,
                        help="image file name")
    parser.add_argument("--force", "-f", action="store_true",
                        help="force build image even if image exists")
    parser.add_argument("--build-image", "-b", action="store_true",
                        help="build image")
    parser.add_argument("--build-qemu",
                        help="build QEMU from source in guest")
    parser.add_argument("--interactive", "-I", action="store_true",
                        help="Interactively run command")
    return parser.parse_known_args()

def main(vmcls):
    args, argv = parse_args(vmcls.name)
    if not argv and not args.build_qemu:
        print "Nothing to do?"
        return 1
    if args.debug:
        logging.getLogger().setLevel(logging.DEBUG)
    vm = vmcls(debug=args.debug)
    if args.build_image:
        if os.path.exists(args.image) and not args.force:
            sys.stderr.writelines(["Image file exists: %s\n" % img,
                                  "Use --force option to overwrite\n"])
            return 1
        return vm.build_image(args.image)
    if args.build_qemu:
        vm.add_source_dir(args.build_qemu)
        cmd = [vm.BUILD_SCRIPT.format(
               configure_opts = " ".join(argv),
               jobs=os.environ.get("J", "4"))]
    else:
        cmd = argv
    vm.boot(args.image + ",snapshot=on")
    vm.wait_ssh()
    if args.interactive:
        if vm.ssh_interactive(*cmd) == 0:
            return 0
        vm.ssh_interactive()
    else:
        return vm.ssh(*cmd)
