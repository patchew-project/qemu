#!/usr/bin/env python3

import socket
import os
import subprocess
import time

PROC_QEMU='/usr/bin/qemu-system-x86_64'

PROC_REMOTE='/usr/bin/qemu-scsi-dev'

proxy_1, remote_1 = socket.socketpair(socket.AF_UNIX, socket.SOCK_STREAM)
proxy_2, remote_2 = socket.socketpair(socket.AF_UNIX, socket.SOCK_STREAM)
proxy_3, remote_3 = socket.socketpair(socket.AF_UNIX, socket.SOCK_STREAM)

remote_cmd_1 = [ PROC_REMOTE,                                                  \
                 str(remote_1.fileno()),                                       \
                 '-device', 'lsi53c895a,id=lsi1',                              \
                 '-drive', 'id=drive_image1,'                                  \
                               'file=/build/ol7-nvme-test-1.qcow2',            \
                 '-device', 'scsi-hd,id=drive1,drive=drive_image1,'            \
                                'bus=lsi1.0,scsi-id=0',                        \
               ]

remote_cmd_2 = [ PROC_REMOTE,                                                  \
                 str(remote_2.fileno()),                                       \
                 '-device', 'lsi53c895a,id=lsi2',                              \
                 '-drive', 'id=drive_image2,'                                  \
                               'file=/build/ol7-nvme-test-2.qcow2',            \
                 '-device', 'scsi-hd,id=drive2,drive=drive_image2,'            \
                                'bus=lsi2.0,scsi-id=0'                         \
               ]

remote_cmd_3 = [ PROC_REMOTE,                                                  \
                 str(remote_3.fileno()),                                       \
                 '-device', 'lsi53c895a,id=lsi3',                              \
                 '-drive', 'id=drive_image3,'                                  \
                               'file=/build/ol7-nvme-test-3.qcow2',            \
                 '-device', 'scsi-hd,id=drive3,drive=drive_image3,'            \
                                'bus=lsi3.0,scsi-id=0'                         \
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
              '-device', 'pci-proxy-dev,id=lsi1,'                              \
                             'socket='+str(proxy_1.fileno()),                  \
              '-device', 'pci-proxy-dev,id=lsi2,'                              \
                             'socket='+str(proxy_2.fileno()),                  \
              '-device', 'pci-proxy-dev,id=lsi3,'                              \
                             'socket='+str(proxy_3.fileno())                   \
            ]


pid = os.fork();
if pid == 0:
    # In remote_1
    print('Launching Remote process 1');
    process = subprocess.Popen(remote_cmd_1, pass_fds=[remote_1.fileno()])
    os._exit(0)


pid = os.fork();
if pid == 0:
    # In remote_2
    print('Launching Remote process 2');
    process = subprocess.Popen(remote_cmd_2, pass_fds=[remote_2.fileno()])
    os._exit(0)


pid = os.fork();
if pid == 0:
    # In remote_3
    print('Launching Remote process 3');
    process = subprocess.Popen(remote_cmd_3, pass_fds=[remote_3.fileno()])
    os._exit(0)


print('Launching Proxy process');
process = subprocess.Popen(proxy_cmd, pass_fds=[proxy_1.fileno(),              \
                           proxy_2.fileno(), proxy_3.fileno()])
