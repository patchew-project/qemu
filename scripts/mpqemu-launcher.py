#!/usr/bin/env python3
import socket
import os
import subprocess
import time

PROC_QEMU='/usr/bin/qemu-system-x86_64'

proxy, remote = socket.socketpair(socket.AF_UNIX, socket.SOCK_STREAM)

remote_cmd = [ PROC_QEMU,                                                      \
               '-machine', 'remote,socket='+str(remote.fileno()),              \
               '-device', 'lsi53c895a,id=lsi1',                                \
               '-drive', 'id=drive_image1,file=/build/ol7-nvme-test-1.qcow2',  \
               '-device', 'scsi-hd,id=drive1,drive=drive_image1,bus=lsi1.0,'   \
                              'scsi-id=0',                                     \
               '-nographic',                                                   \
             ]

proxy_cmd = [ PROC_QEMU,                                                       \
              '-name', 'OL7.4',                                                \
              '-machine', 'q35,accel=kvm',                                     \
              '-smp', 'sockets=1,cores=1,threads=1',                           \
              '-m', '2048',                                                    \
              '-object', 'memory-backend-memfd,id=sysmem-file,size=2G',        \
              '-numa', 'node,memdev=sysmem-file',                              \
              '-device', 'virtio-scsi-pci,id=virtio_scsi_pci0',                \
              '-drive', 'id=drive_image1,if=none,format=qcow2,'                \
                            'file=/home/ol7-hdd-1.qcow2',                      \
              '-device', 'scsi-hd,id=image1,drive=drive_image1,'               \
                             'bus=virtio_scsi_pci0.0',                         \
              '-boot', 'd',                                                    \
              '-vnc', ':0',                                                    \
              '-device', 'pci-proxy-dev,id=lsi1,fd='+str(proxy.fileno()),      \
            ]


pid = os.fork();

if pid:
    # In Proxy
    print('Launching QEMU with Proxy object');
    process = subprocess.Popen(proxy_cmd, pass_fds=[proxy.fileno()])
else:
    # In remote
    print('Launching Remote process');
    process = subprocess.Popen(remote_cmd, pass_fds=[remote.fileno()])
