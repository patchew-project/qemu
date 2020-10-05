#!/bin/sh

# Test all QOM dependencies are resolved
storage-daemon/qemu-storage-daemon \
  --chardev stdio,id=qmp0  --monitor qmp0 \
  > /dev/null << 'EOF'
{"execute": "qmp_capabilities"}
{"execute": "qom-list-types"}
{"execute": "quit"}
EOF
