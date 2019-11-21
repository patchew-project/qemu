#!/usr/bin/env python

# High-level test for qemu COLO testing all failover cases while checking
# guest network connectivity
#
# Copyright (c) Lukas Straub <lukasstraub2@web.de>
#
# This work is licensed under the terms of the GNU GPL, version 2 or
# later.  See the COPYING file in the top-level directory.

import select
import sys
import subprocess
import shutil
import os
import signal
import os.path
import json
import time
import tempfile

from avocado import Test
from avocado.utils.archive import gzip_uncompress
from avocado.utils import network
from avocado_qemu import pick_default_qemu_bin, SRC_ROOT_DIR

class ColoTest(Test):
    timeout = 120

    # Constants
    OCF_SUCCESS = 0
    OCF_ERR_GENERIC = 1
    OCF_ERR_ARGS = 2
    OCF_ERR_UNIMPLEMENTED = 3
    OCF_ERR_PERM = 4
    OCF_ERR_INSTALLED = 5
    OCF_ERR_CONFIGURED = 6
    OCF_NOT_RUNNING = 7
    OCF_RUNNING_MASTER = 8
    OCF_FAILED_MASTER = 9

    HOSTA = 10
    HOSTB = 11

    QEMU_OPTIONS = (" -enable-kvm -cpu qemu64,+kvmclock -m 256"
                    " -device virtio-net,netdev=hn0"
                    " -device virtio-blk,drive=colo-disk0")
    COLO_RA = "scripts/colo-resource-agent/colo"
    FAKEPATH = ".:scripts/colo-resource-agent"

    bridge_proc = None
    ssh_proc = None

    def setUp(self):
        # Qemu binary
        default_qemu_bin = pick_default_qemu_bin()
        self.QEMU_BINARY = self.params.get('qemu_bin', default=default_qemu_bin)

        # Find free port range
        base_port = 1024
        while True:
            base_port = network.find_free_port(start_port=base_port, \
                                               address="127.0.0.1")
            if base_port == None:
                self.cancel("Failed to find a free port")
            for n in range(base_port, base_port +6):
                if not network.is_port_free(n, "127.0.0.1"):
                    base_port = n +1
                    break
            else:
                # for loop above didn't break
                break

        self.BRIDGE_HOSTA_PORT = base_port
        self.BRIDGE_HOSTB_PORT = base_port + 1
        self.SSH_PORT = base_port + 2
        self.COLO_BASE_PORT = base_port + 3

        # Temporary directories
        self.TMPDIR = tempfile.mkdtemp()
        self.TMPA = os.path.join(self.TMPDIR, "hosta")
        self.TMPB = os.path.join(self.TMPDIR, "hostb")
        os.makedirs(self.TMPA)
        os.makedirs(self.TMPB)

        # Disk images
        self.HOSTA_IMAGE = os.path.join(self.TMPA, "image.raw")
        self.HOSTB_IMAGE = os.path.join(self.TMPB, "image.raw")

        image_url = ("https://downloads.openwrt.org/releases/18.06.5/targets/"
                     "x86/64/openwrt-18.06.5-x86-64-combined-ext4.img.gz")
        image_hash = ("55589a3a9b943218b1734d196bcaa92a"
                      "3cfad91c07fa6891474b4291ce1b8ec2")
        self.IMAGE_SIZE = "285736960b"
        download = self.fetch_asset(image_url, asset_hash=image_hash, \
                                    algorithm="sha256")
        gzip_uncompress(download, self.HOSTA_IMAGE)
        shutil.copyfile(self.HOSTA_IMAGE, self.HOSTB_IMAGE)

        self.log.info("Will put logs in \"%s\"" % self.outputdir)
        self.RA_LOG = os.path.join(self.outputdir, "resource-agent.log")
        self.HOSTA_LOGDIR = os.path.join(self.outputdir, "hosta")
        self.HOSTB_LOGDIR = os.path.join(self.outputdir, "hostb")
        os.makedirs(self.HOSTA_LOGDIR)
        os.makedirs(self.HOSTB_LOGDIR)

        # Network bridge
        self.BRIDGE_PIDFILE = os.path.join(self.TMPDIR, "bridge.pid")
        pid = self.read_pidfile(self.BRIDGE_PIDFILE)
        if not (pid and self.check_pid(pid)):
            self.run_command(("%s -M none -daemonize -pidfile '%s'"
                " -netdev socket,id=hosta,listen=127.0.0.1:%s"
                " -netdev hubport,id=porta,hubid=0,netdev=hosta"
                " -netdev socket,id=hostb,listen=127.0.0.1:%s"
                " -netdev hubport,id=portb,hubid=0,netdev=hostb"
                " -netdev user,net=192.168.1.1/24,host=192.168.1.2,"
                "hostfwd=tcp:127.0.0.1:%s-192.168.1.1:22,id=host"
                " -netdev hubport,id=hostport,hubid=0,netdev=host")
                % (self.QEMU_BINARY, self.BRIDGE_PIDFILE,
                   self.BRIDGE_HOSTA_PORT, self.BRIDGE_HOSTB_PORT,
                   self.SSH_PORT), 0)

    def tearDown(self):
        try:
            pid = self.read_pidfile(self.BRIDGE_PIDFILE)
            if pid and self.check_pid(pid):
                os.kill(pid, signal.SIGKILL)
        except Exception():
            pass
        try:
            self.ra_stop(self.HOSTA)
        except Exception():
            pass
        try:
            self.ra_stop(self.HOSTB)
        except Exception():
            pass
        try:
            if self.ssh_proc:
                self.ssh_proc.terminate()
        except Exception():
            pass

        shutil.rmtree(self.TMPDIR)

    def run_command(self, cmdline, expected_status, env=None, error_fail=True):
        proc = subprocess.Popen(cmdline, shell=True, stdout=subprocess.PIPE, \
                                stderr=subprocess.STDOUT, \
                                universal_newlines=True, env=env)
        stdout, stderr = proc.communicate()
        if proc.returncode != expected_status:
            message = "command \"%s\" failed with code %s:\n%s" \
                           % (cmdline, proc.returncode, stdout)
            if error_fail:
                self.log.error(message)
                self.fail("command \"%s\" failed" % cmdline)
            else:
                self.log.info(message)

        return proc.returncode

    def cat_line(self, path):
        line=""
        try:
            fd = open(path, "r")
            line = str.strip(fd.readline())
            fd.close()
        except:
            pass
        return line

    def read_pidfile(self, pidfile):
        try:
            pid = int(self.cat_line(pidfile))
        except ValueError:
            return None
        else:
            return pid

    def check_pid(self, pid):
        try:
            os.kill(pid, 0)
        except OSError:
            return False
        else:
            return True

    def ssh_ping(self, proc):
        proc.stdin.write("ping\n")
        if not select.select([proc.stdout], [], [], 30)[0]:
            raise self.fail("ssh ping timeout reached")
        if proc.stdout.readline() != "ping\n":
            raise self.fail("unexpected ssh ping answer")

    def ssh_open(self):
        commandline = ("ssh -o \"UserKnownHostsFile /dev/null\""
                       " -o \"StrictHostKeyChecking no\""
                       " -p%s root@127.0.0.1") % self.SSH_PORT

        self.log.info("Connecting via ssh")
        for i in range(10):
            if self.run_command(commandline + " exit", 0, error_fail=False) \
                == 0:
                proc = subprocess.Popen(commandline + " cat", shell=True, \
                                            stdin=subprocess.PIPE, \
                                            stdout=subprocess.PIPE, \
                                            stderr=0, \
                                            universal_newlines=True,
                                            bufsize=1)
                self.ssh_ping(proc)
                return proc
            else:
                time.sleep(5)
        self.fail("ssh connect timeout reached")

    def ssh_close(self, proc):
        proc.terminate()

    def setup_base_env(self, host):
        PATH = os.getenv("PATH", "")
        env = { "PATH": "%s:%s" % (self.FAKEPATH, PATH),
                "HA_LOGFILE": self.RA_LOG,
                "OCF_RESOURCE_INSTANCE": "colo-test",
                "OCF_RESKEY_CRM_meta_clone_max": "2",
                "OCF_RESKEY_CRM_meta_notify": "true",
                "OCF_RESKEY_CRM_meta_timeout": "30000",
                "OCF_RESKEY_binary": self.QEMU_BINARY,
                "OCF_RESKEY_disk_size": str(self.IMAGE_SIZE),
                "OCF_RESKEY_checkpoint_interval": "1000",
                "OCF_RESKEY_base_port": str(self.COLO_BASE_PORT),
                "OCF_RESKEY_debug": "true"}

        if host == self.HOSTA:
            env.update({"OCF_RESKEY_options":
                            ("%s -netdev socket,id=hn0,connect=127.0.0.1:%s"
                             " -drive if=none,id=parent0,format=raw,file='%s'")
                            % (self.QEMU_OPTIONS, self.BRIDGE_HOSTA_PORT,
                                self.HOSTA_IMAGE),
                        "OCF_RESKEY_active_hidden_dir": self.TMPA,
                        "OCF_RESKEY_listen_address": "127.0.0.1",
                        "OCF_RESKEY_log_dir": self.HOSTA_LOGDIR,
                        "OCF_RESKEY_CRM_meta_on_node": "127.0.0.1",
                        "HA_RSCTMP": self.TMPA,
                        "COLO_SMOKE_REMOTE_TMP": self.TMPB})
        else:
            env.update({"OCF_RESKEY_options":
                            ("%s -netdev socket,id=hn0,connect=127.0.0.1:%s"
                             " -drive if=none,id=parent0,format=raw,file='%s'")
                            % (self.QEMU_OPTIONS, self.BRIDGE_HOSTB_PORT,
                                self.HOSTB_IMAGE),
                        "OCF_RESKEY_active_hidden_dir": self.TMPB,
                        "OCF_RESKEY_listen_address": "127.0.0.2",
                        "OCF_RESKEY_log_dir": self.HOSTB_LOGDIR,
                        "OCF_RESKEY_CRM_meta_on_node": "127.0.0.2",
                        "HA_RSCTMP": self.TMPB,
                        "COLO_SMOKE_REMOTE_TMP": self.TMPA})
        return env

    def ra_start(self, host):
        env = self.setup_base_env(host)
        self.run_command(self.COLO_RA + " start", self.OCF_SUCCESS, env)

    def ra_stop(self, host):
        env = self.setup_base_env(host)
        self.run_command(self.COLO_RA + " stop", self.OCF_SUCCESS, env)

    def ra_monitor(self, host, expected_status):
        env = self.setup_base_env(host)
        self.run_command(self.COLO_RA + " monitor", expected_status, env)

    def ra_promote(self, host):
        env = self.setup_base_env(host)
        self.run_command(self.COLO_RA + " promote", self.OCF_SUCCESS, env)

    def ra_notify_start(self, host):
        env = self.setup_base_env(host)

        env.update({"OCF_RESKEY_CRM_meta_notify_type": "post",
                    "OCF_RESKEY_CRM_meta_notify_operation": "start"})

        if host == self.HOSTA:
            env.update({"OCF_RESKEY_CRM_meta_notify_master_uname": "127.0.0.1",
                        "OCF_RESKEY_CRM_meta_notify_start_uname": "127.0.0.2"})
        else:
            env.update({"OCF_RESKEY_CRM_meta_notify_master_uname": "127.0.0.2",
                        "OCF_RESKEY_CRM_meta_notify_start_uname": "127.0.0.1"})

        self.run_command(self.COLO_RA + " notify", self.OCF_SUCCESS, env)

    def ra_notify_stop(self, host):
        env = self.setup_base_env(host)

        env.update({"OCF_RESKEY_CRM_meta_notify_type": "pre",
                    "OCF_RESKEY_CRM_meta_notify_operation": "stop"})

        if host == self.HOSTA:
            env.update({"OCF_RESKEY_CRM_meta_notify_master_uname": "127.0.0.1",
                        "OCF_RESKEY_CRM_meta_notify_stop_uname": "127.0.0.2"})
        else:
            env.update({"OCF_RESKEY_CRM_meta_notify_master_uname": "127.0.0.2",
                        "OCF_RESKEY_CRM_meta_notify_stop_uname": "127.0.0.1"})

        self.run_command(self.COLO_RA + " notify", self.OCF_SUCCESS, env)

    def kill_qemu_pre(self, host, hang_qemu=False):
        if host == self.HOSTA:
            pid = self.read_pidfile(os.path.join(self.TMPA, \
                                                        "colo-test-qemu.pid"))
        else:
            pid = self.read_pidfile(os.path.join(self.TMPB, \
                                                        "colo-test-qemu.pid"))

        if pid and self.check_pid(pid):
            if hang_qemu:
                os.kill(pid, signal.SIGSTOP)
            else:
                os.kill(pid, signal.SIGKILL)
                while self.check_pid(pid):
                    time.sleep(1)

    def kill_qemu_post(self, host, hang_qemu=False):
        if host == self.HOSTA:
            pid = self.read_pidfile(os.path.join(self.TMPA, \
                                                        "colo-test-qemu.pid"))
        else:
            pid = self.read_pidfile(os.path.join(self.TMPB, \
                                                        "colo-test-qemu.pid"))

        if hang_qemu and pid and self.check_pid(pid):
            os.kill(pid, signal.SIGKILL)
            while self.check_pid(pid):
                time.sleep(1)

    def get_master_score(self, host):
        if host == self.HOSTA:
            return int(self.cat_line(os.path.join(self.TMPA, "master_score")))
        else:
            return int(self.cat_line(os.path.join(self.TMPB, "master_score")))

    def _test_colo(self, hang_qemu=False, loop=False, do_ssh_ping=True):
        self.ra_stop(self.HOSTA)
        self.ra_stop(self.HOSTB)

        self.log.info("Startup")
        self.ra_start(self.HOSTA)
        self.ra_start(self.HOSTB)

        self.ra_monitor(self.HOSTA, self.OCF_SUCCESS)
        self.ra_monitor(self.HOSTB, self.OCF_SUCCESS)

        self.log.info("Promoting")
        self.ra_promote(self.HOSTA)
        self.ra_notify_start(self.HOSTA)

        while self.get_master_score(self.HOSTB) != 100:
            self.ra_monitor(self.HOSTA, self.OCF_RUNNING_MASTER)
            self.ra_monitor(self.HOSTB, self.OCF_SUCCESS)
            time.sleep(1)

        if do_ssh_ping:
            self.ssh_proc = self.ssh_open()

        primary = self.HOSTA
        secondary = self.HOSTB

        while True:
            self.log.info("Secondary failover")
            self.kill_qemu_pre(primary, hang_qemu)
            self.ra_notify_stop(secondary)
            self.ra_monitor(secondary, self.OCF_SUCCESS)
            self.ra_promote(secondary)
            self.ra_monitor(secondary, self.OCF_RUNNING_MASTER)
            self.kill_qemu_post(primary, hang_qemu)
            if do_ssh_ping:
                self.ssh_ping(self.ssh_proc)
            tmp = primary
            primary = secondary
            secondary = tmp

            self.log.info("Secondary continue replication")
            self.ra_start(secondary)
            self.ra_notify_start(primary)
            if do_ssh_ping:
                self.ssh_ping(self.ssh_proc)

            # Wait for resync
            while self.get_master_score(secondary) != 100:
                self.ra_monitor(primary, self.OCF_RUNNING_MASTER)
                self.ra_monitor(secondary, self.OCF_SUCCESS)
                time.sleep(1)
            if do_ssh_ping:
                self.ssh_ping(self.ssh_proc)

            self.log.info("Primary failover")
            self.kill_qemu_pre(secondary, hang_qemu)
            self.ra_monitor(primary, self.OCF_RUNNING_MASTER)
            self.ra_notify_stop(primary)
            self.ra_monitor(primary, self.OCF_RUNNING_MASTER)
            self.kill_qemu_post(secondary, hang_qemu)
            if do_ssh_ping:
                self.ssh_ping(self.ssh_proc)

            self.log.info("Primary continue replication")
            self.ra_start(secondary)
            self.ra_notify_start(primary)
            if do_ssh_ping:
                self.ssh_ping(self.ssh_proc)

            # Wait for resync
            while self.get_master_score(secondary) != 100:
                self.ra_monitor(primary, self.OCF_RUNNING_MASTER)
                self.ra_monitor(secondary, self.OCF_SUCCESS)
                time.sleep(1)
            if do_ssh_ping:
                self.ssh_ping(self.ssh_proc)

            if not loop:
                break

        if do_ssh_ping:
            self.ssh_close(self.ssh_proc)

        self.ra_stop(self.HOSTA)
        self.ra_stop(self.HOSTB)

        self.ra_monitor(self.HOSTA, self.OCF_NOT_RUNNING)
        self.ra_monitor(self.HOSTB, self.OCF_NOT_RUNNING)
        self.log.info("all ok")

    def test_colo_peer_crashing(self):
        """
        :avocado: tags=colo
        :avocado: tags=arch:x86_64
        """
        self.log.info("Testing with peer qemu crashing")
        self._test_colo()

    def test_colo_peer_hanging(self):
        """
        :avocado: tags=colo
        :avocado: tags=arch:x86_64
        """
        self.log.info("Testing with peer qemu hanging")
        self._test_colo(hang_qemu=True)
