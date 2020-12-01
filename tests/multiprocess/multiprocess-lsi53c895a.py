#!/usr/bin/env python3

import urllib.request
import subprocess
import argparse
import socket
import sys
import os

arch = os.uname()[4]
proc_path = os.path.join(os.getcwd(), '..', '..', 'build', arch+'-softmmu',
                         'qemu-system-'+arch)

parser = argparse.ArgumentParser(description='Launcher for multi-process QEMU')
parser.add_argument('--bin', required=False, help='location of QEMU binary',
                    metavar='bin');
args = parser.parse_args()

if args.bin is not None:
    proc_path = args.bin

if not os.path.isfile(proc_path):
    sys.exit('QEMU binary not found')

kernel_path = os.path.join(os.getcwd(), 'vmlinuz')
initrd_path = os.path.join(os.getcwd(), 'initrd')

proxy_cmd = [ proc_path,                                                    \
              '-name', 'Fedora', '-smp', '4', '-m', '2048', '-cpu', 'host', \
              '-object', 'memory-backend-memfd,id=sysmem-file,size=2G',     \
              '-numa', 'node,memdev=sysmem-file',                           \
              '-kernel', kernel_path, '-initrd', initrd_path,               \
              '-vnc', ':0',                                                 \
              '-monitor', 'unix:/home/qemu-sock,server,nowait',             \
            ]

if arch == 'x86_64':
    print('Downloading images for arch x86_64')
    kernel_url = 'https://dl.fedoraproject.org/pub/fedora/linux/'    \
                 'releases/33/Everything/x86_64/os/images/'          \
                 'pxeboot/vmlinuz'
    initrd_url = 'https://dl.fedoraproject.org/pub/fedora/linux/'    \
                 'releases/33/Everything/x86_64/os/images/'          \
                 'pxeboot/initrd.img'
    proxy_cmd.append('-machine')
    proxy_cmd.append('pc,accel=kvm')
    proxy_cmd.append('-append')
    proxy_cmd.append('rdinit=/bin/bash console=ttyS0 console=tty0')
elif arch == 'aarch64':
    print('Downloading images for arch aarch64')
    kernel_url = 'https://dl.fedoraproject.org/pub/fedora/linux/'    \
                 'releases/33/Everything/aarch64/os/images/'         \
                 'pxeboot/vmlinuz'
    initrd_url = 'https://dl.fedoraproject.org/pub/fedora/linux/'    \
                 'releases/33/Everything/aarch64/os/images/'         \
                 'pxeboot/initrd.img'
    proxy_cmd.append('-machine')
    proxy_cmd.append('virt,gic-version=3')
    proxy_cmd.append('-accel')
    proxy_cmd.append('kvm')
    proxy_cmd.append('-append')
    proxy_cmd.append('rdinit=/bin/bash')
else:
    sys.exit('Arch %s not tested' % arch)

urllib.request.urlretrieve(kernel_url, kernel_path)
urllib.request.urlretrieve(initrd_url, initrd_path)

proxy, remote = socket.socketpair(socket.AF_UNIX, socket.SOCK_STREAM)

proxy_cmd.append('-device')
proxy_cmd.append('x-pci-proxy-dev,id=lsi1,fd='+str(proxy.fileno()))

remote_cmd = [ proc_path,                                                      \
               '-machine', 'x-remote',                                         \
               '-device', 'lsi53c895a,id=lsi1',                                \
               '-object',                                                      \
               'x-remote-object,id=robj1,devid=lsi1,fd='+str(remote.fileno()), \
               '-display', 'none',                                             \
               '-monitor', 'unix:/home/rem-sock,server,nowait',                \
             ]

pid = os.fork();

if pid:
    # In Proxy
    print('Launching QEMU with Proxy object');
    process = subprocess.Popen(proxy_cmd, pass_fds=[proxy.fileno()])
else:
    # In remote
    print('Launching Remote process');
    process = subprocess.Popen(remote_cmd, pass_fds=[remote.fileno(), 0, 1, 2])
