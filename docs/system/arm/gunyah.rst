'gunyah' accelerator (``gunyah``)
=================================

Gunyah is a high performance, scalable and flexible hypervisor built for
demanding battery powered, real-time, safety and security use cases.

The Gunyah Hypervisor open source project provides a reference Type-1 hypervisor
configuration suitable for general purpose hosting of multiple trusted and
dependent VMs. Further information on open-source version of Gunyah Hypervisor
can be obtained from:

https://github.com/quic/gunyah-hypervisor

To get started with open-source version of Gunyah Hypervisor, refer to the
instructions available at:

https://github.com/quic/gunyah-support-scripts

Build and testing
-----------------

Configure and build Qemu
````````````````````````

Apply the proposed patches for 'gunyah' accelerator support in Qemu and build
it.

.. code-block:: bash

        $ ./configure --target-list=aarch64-softmmu --enable-debug --enable-gunyah --static
        $ make -j4
        $ mv build/qemu-system-aarch64 build/qemu-gunyah

Clone gunyah-support scripts
````````````````````````````

.. code-block:: bash

        $ git clone https://github.com/quic/gunyah-support-scripts

Instructions in this document to build and test Gunyah hypervisor was validated
with the latest commit in gunyah-support-scripts being:

6a959c8 tools: Fix permission and version related

Patch gunyah-support scripts
````````````````````````````
Apply below patch to gunyah-support scripts. This is required **temporarily** until
the scripts can be updated to support Qemu as VMM (in addition to CrosVM) and
also to fix some issues.

.. code-block:: bash

    diff --git a/scripts/build-docker-img.sh b/scripts/build-docker-img.sh
    index 98e7881..a6aa774 100755
    --- a/scripts/build-docker-img.sh
    +++ b/scripts/build-docker-img.sh
    @@ -38,7 +38,7 @@ DOCKER_OPTIONS=" build . "
     #DOCKER_OPTIONS+=" --progress=plain "

     #  no-cache alleviates some install errors for not finding some packages
    -#DOCKER_OPTIONS+=" --no-cache "
    +DOCKER_OPTIONS+=" --no-cache "

     # user environment related so the permissions will same as the host machine
     DOCKER_OPTIONS+=" --build-arg UID=$(id -u) "
    diff --git a/scripts/core-utils/clone-linux.sh b/scripts/core-utils/clone-linux.sh
    index 714162e..2b79bc7 100755
    --- a/scripts/core-utils/clone-linux.sh
    +++ b/scripts/core-utils/clone-linux.sh
    @@ -26,8 +26,7 @@ cd ${LINUX_DIR}/src
     LINUX_VER="v6.5"
     echo -e "\nCloning Linux ${LINUX_VER}:"
     git clone \
    -    --depth=1 --progress -c advice.detachedHead=false \
    -    -b ${LINUX_VER}  \
    +    --progress -c advice.detachedHead=false \
         https://github.com/torvalds/linux.git   || {
        echo "Unable to clone Linux"
        return
    @@ -58,7 +57,11 @@ echo "Installed b4 to ${LINUX_DIR}/tools/b4"

     cd ${LINUX_DIR}/src/linux

    -${LINUX_DIR}/tools/b4/b4.sh shazam https://lore.kernel.org/all/20230613172054.3959700-1-quic_eberman@quicinc.com/
    +
    +${LINUX_DIR}/tools/b4/b4.sh am https://lore.kernel.org/all/20230613172054.3959700-1-quic_eberman@quicinc.com/
    +git checkout -b v14_20230613_quic_eberman_quicinc_com 858fd168a95c5b9669aac8db6c14a9aeab446375
    +git am ./v14_20230613_quic_eberman_drivers_for_gunyah_hypervisor.mbx
    +
     echo "Applied gunyah drivers patch successfully"

     echo "Generate gunyah.config"
    diff --git a/scripts/install-wsp-imgs.sh b/scripts/install-wsp-imgs.sh
    index 12150f3..32107e0 100755
    --- a/scripts/install-wsp-imgs.sh
    +++ b/scripts/install-wsp-imgs.sh
    @@ -100,15 +100,23 @@ if [[ ! -f ${WORKSPACE}/run-qemu.sh ]]; then
         cp ${BASE_DIR}/utils/run-qemu.sh ${WORKSPACE}/run-qemu.sh
     fi

    -if [[ ! -f ${WORKSPACE}/crosvm/crosvm ]]; then
    -    mkdir -p ${WORKSPACE}/crosvm
    -    cd ${WORKSPACE}/crosvm
    -    . clone-crosvm.sh
    -    . build-crosvm.sh
    -
    -    echo -e 'export CROSVM_FILE_PATH=${WORKSPACE}/crosvm/crosvm' >> ${WORKSPACE}/.wsp-env
    -    . ${WORKSPACE}/.wsp-env
    -fi
    +cp ${BASE_DIR}/utils/qemu-gunyah ${WORKSPACE}/
    +cp ${BASE_DIR}/utils/efi-virtio.rom ${WORKSPACE}/
    +cp ${BASE_DIR}/utils/en-us ${WORKSPACE}/
    +cp ${BASE_DIR}/utils/svm_disk.img ${WORKSPACE}/
    +
    +#if [[ ! -f ${WORKSPACE}/crosvm/crosvm ]]; then
    +#    mkdir -p ${WORKSPACE}/crosvm
    +#    cd ${WORKSPACE}/crosvm
    +#    . clone-crosvm.sh
    +#    . build-crosvm.sh
    +
    +#    echo -e 'export CROSVM_FILE_PATH=${WORKSPACE}/crosvm/crosvm' >> ${WORKSPACE}/.wsp-env
    +#    . ${WORKSPACE}/.wsp-env
    +#fi
    +
    +echo -e 'export CROSVM_FILE_PATH=${WORKSPACE}/qemu-gunyah' >> ${WORKSPACE}/.wsp-env
    +. ${WORKSPACE}/.wsp-env

     if [[ ! -f ${WORKSPACE}/rootfs/rootfs-extfs-disk.img ]]; then
         echo -e "\nrootfs image not found, creating new one"
    diff --git a/scripts/migrate-tools-to-vol.sh b/scripts/migrate-tools-to-vol.sh
    index e5240c6..330f807 100755
    --- a/scripts/migrate-tools-to-vol.sh
    +++ b/scripts/migrate-tools-to-vol.sh
    @@ -76,14 +76,14 @@ if [[ ! -d ${WORKSPACE}/linux ]]; then
         echo "Done copying linux files"
     fi

    -if [[ -d ~/share/docker-share/crosvm ]]; then
    -    mv ~/share/docker-share/crosvm ${WORKSPACE}/
    -    echo "Found crosvm, moved into workspace folder"
    -    mv ${WORKSPACE}/crosvm/crosvm ${WORKSPACE}/crosvm/crosvm-src
    -    cp ${WORKSPACE}/crosvm/crosvm-src/crosvm  ${WORKSPACE}/crosvm/crosvm
    -    rm -rf ${WORKSPACE}/crosvm/crosvm-src
    -    echo -e 'export CROSVM_FILE_PATH=${WORKSPACE}/crosvm/crosvm' >> ${WORKSPACE}/.wsp-env
    -fi
    +#if [[ -d ~/share/docker-share/crosvm ]]; then
    +#    mv ~/share/docker-share/crosvm ${WORKSPACE}/
    +#    echo "Found crosvm, moved into workspace folder"
    +#    mv ${WORKSPACE}/crosvm/crosvm ${WORKSPACE}/crosvm/crosvm-src
    +#    cp ${WORKSPACE}/crosvm/crosvm-src/crosvm  ${WORKSPACE}/crosvm/crosvm
    +#    rm -rf ${WORKSPACE}/crosvm/crosvm-src
    +#    echo -e 'export CROSVM_FILE_PATH=${WORKSPACE}/crosvm/crosvm' >> ${WORKSPACE}/.wsp-env
    +#fi

     if [[ -d ~/share/docker-share/rootfs ]]; then
         mv ~/share/docker-share/rootfs ${WORKSPACE}/
    diff --git a/scripts/utils/build-rootfs-img.sh b/scripts/utils/build-rootfs-img.sh
    index d110965..9ffe530 100755
    --- a/scripts/utils/build-rootfs-img.sh
    +++ b/scripts/utils/build-rootfs-img.sh
    @@ -177,6 +177,9 @@ if [[ ! -f ${SVM_DESTINATION}/svm.sh ]]; then
        echo -e '--params "rw root=/dev/ram rdinit=/sbin/init earlyprintk=serial panic=0" \\' >> ./svm.sh
        echo -e ' /usr/gunyah/Image $@\n' >> ./svm.sh

    +   sudo cp ${WORKSPACE}/svm_disk.img ${SVM_DESTINATION}
    +   sudo cp ${WORKSPACE}/efi-virtio.rom ${SVM_DESTINATION}
    +   sudo cp ${WORKSPACE}/en-us ${SVM_DESTINATION}
        sudo cp ./svm.sh ${SVM_DESTINATION}
        rm -f ./svm.sh
        sudo chmod 0775 ${SVM_DESTINATION}/svm.sh
    @@ -216,13 +219,15 @@ if [[ ! -f ${ROOTFS_REFERENCE_DIR}/lib/libgcc_s.so.1 ]]; then
        export MACHINE=qemuarm64
        export DISTRO=rpb

    -   mkdir ${ROOTFS_BASE}/oe-rpb
    +   mkdir -p ${ROOTFS_BASE}/oe-rpb
        cd ${ROOTFS_BASE}/oe-rpb

        # fetch
        ~/bin/repo init -u https://github.com/96boards/oe-rpb-manifest.git -b qcom/master
        ~/bin/repo sync

    +   rm layers/meta-qcom/recipes-kernel/linux/linux-yocto_6.6.bbappend
    +
        # add config for libgcc and other virtualization options
        echo -e "\n" > ./extra_local.conf
        echo "INHERIT += 'buildstats buildstats-summary'" >> ./extra_local.conf
    @@ -269,5 +274,5 @@ if [[ -f ${WORKSPACE}/rootfs/rootfs-extfs-disk.img ]]; then
     else
        echo "Creating rootfs image file from reference : `pwd`"
        cd ${WORKSPACE}/rootfs
    -   . ~/utils/bldextfs.sh -f ${WORKSPACE}/rootfs/reference -o ${WORKSPACE}/rootfs/rootfs-extfs-disk.img -s 800M
    +   . ~/utils/bldextfs.sh -f ${WORKSPACE}/rootfs/reference -o ${WORKSPACE}/rootfs/rootfs-extfs-disk.img -s 2G
     fi

Copy Qemu files
```````````````

Copy Qemu and related files to `utils` directory of gunyah-support scripts.

.. code-block:: bash

        # qemu-gunyah is nothing but qemu-system-aarch64 binary that supports gunyah accelerator
        cp qemu-gunyah scripts/utils

        # efi-virtio.rom is found under `pc-bios` directory of Qemu
        cp efi-virtio.rom scripts/utils

        # en-us is found under `pc-bios/keymaps` directory of Qemu
        cp en-us scripts/utils

        # svm_disk.img will serve as the root disk for VM. It will have init and
        # other programs that are required to boot VM. It can be prepared from
        # any aarch64-based distro such as Ubuntu.
        cp svm_disk.img scripts/utils

Build docker image
``````````````````

.. code-block:: bash

        cd scripts
        ./build-docker-img.sh

Rest of steps below need to be run inside docker. Launch the docker as:

.. code-block:: bash

        # SOME_FOLDER is any directory on host. This will be accessible from
        # inside docker and is useful to share files between host and docker
        # environments.
        export HOST_TO_DOCKER_SHARED_DIR=SOME_FOLDER
        cd scripts
        ./run-docker.sh

Clone and build a Gunyah Hypervisor image
`````````````````````````````````````````

.. code-block:: bash

        cd ~/share/gunyah
        clone-gunyah.sh

Cloned sources includes that for Resource Manager (RM) under `resource-manager`
directory. RM is a privileged VM that acts as an extension of Gunyah
hypervisor and assists the hypervisor in various tasks related to creation and
management of VMs. More information on RM is provided at:

https://github.com/quic/gunyah-resource-manager

Gunyah hypervisor source is available under `hyp` directory.

Patch Gunyah hypervisor and Resource Manager
````````````````````````````````````````````

Apply below changes to hypervisor and RM on which 'gunyah' Qemu accelerator
currently depends. These changes are being discussed with maintainers and if
accepted this document will be modified appropriately.

RM patch (in 'resource-manager' directory):

.. code-block:: bash

    diff --git a/src/vm_creation/vm_creation.c b/src/vm_creation/vm_creation.c
    index df8edfb..b73b37e 100644
    --- a/src/vm_creation/vm_creation.c
    +++ b/src/vm_creation/vm_creation.c
    @@ -510,7 +510,10 @@ process_dtb(vm_t *vm)
            // Estimate a final dtb size after applying the overlay.
            size_t original_dtb_size =
                    util_balign_up(fdt_totalsize(temp_addr), sizeof(uint32_t));
    -       size_t final_dtb_size = original_dtb_size + dtbo_ret.size;
    +       size_t final_dtb_size = util_balign_up(original_dtb_size + dtbo_ret.size, 8);

Hypervisor patch (in 'hyp' directory):

.. code-block:: bash

    diff --git a/config/platform/qemu.conf b/config/platform/qemu.conf
    index bc612f2..9a292a4 100644
    --- a/config/platform/qemu.conf
    +++ b/config/platform/qemu.conf
    @@ -35,7 +35,7 @@ configs HLOS_RAM_FS_BASE=0x40800000
     configs PLATFORM_HEAP_PRIVATE_SIZE=0x200000
     configs PLATFORM_RW_DATA_SIZE=0x200000
     configs PLATFORM_ROOTVM_LMA_BASE=0x80480000U
    -configs PLATFORM_ROOTVM_LMA_SIZE=0xa0000U
    +configs PLATFORM_ROOTVM_LMA_SIZE=0x100000U
     configs PLATFORM_PHYS_ADDRESS_BITS=36
     configs PLATFORM_VM_ADDRESS_SPACE_BITS=36
     configs PLATFORM_PGTABLE_4K_GRANULE=1

Build Gunyah hypervisor
```````````````````````

.. code-block:: bash

        cd ~/share
        build-gunyah.sh qemu

Launch host-VM under Gunyah hypervisor
``````````````````````````````````````

.. code-block:: bash

        cd ~/mnt/workspace
        run-qemu.sh dtb
        run-qemu.sh

Running a secondary VM with Qemu as VMM
```````````````````````````````````````

.. code-block:: bash

        $ cd /usr/gunyah
        $ ./qemu-gunyah -cpu cortex-a57 -nographic -hda svm_disk.img -m 256M -smp cpus=8 --accel gunyah -machine virt,highmem=off -append "rw root=/dev/vda   rdinit=/sbin/init earlyprintk=serial panic=0" -kernel ./Image

Limitations
-----------

* Linux Gunyah kernel driver published upstream does not yet support
  confidential VM. Confidential VM related changes proposed in this patch series
  has been tested on a variant of driver that is available in Android Common
  kernel. Details to test confidential VM will be provided once the upstream
  Gunyah driver supports same.
