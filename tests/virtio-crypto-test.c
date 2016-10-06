/*
 * QTest testcase for VirtIO Crypto Device
 *
 * Copyright (c) 2016 HUAWEI TECHNOLOGIES CO., LTD.
 *
 * Authors:
 *    Gonglei <arei.gonglei@huawei.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#include <stdlib.h>

#include "qemu/osdep.h"
#include "libqtest.h"
#include "libqos/virtio.h"
#include "libqos/virtio-pci.h"
#include "libqos/virtio-mmio.h"
#include "libqos/pci-pc.h"
#include "libqos/malloc.h"
#include "libqos/malloc-pc.h"
#include "libqos/malloc-generic.h"
#include "qemu/bswap.h"
#include "standard-headers/linux/virtio_ids.h"
#include "standard-headers/linux/virtio_config.h"
#include "standard-headers/linux/virtio_ring.h"
#include "standard-headers/linux/virtio_crypto.h"
#include "standard-headers/linux/virtio_pci.h"

#define QVIRTIO_CRYPTO_TIMEOUT_US  (30 * 1000 * 1000)

#define PCI_SLOT_HP             0x06
#define PCI_SLOT                0x04
#define PCI_FN                  0x00

/*
 * VirtIOCryptoCipherTestData:  structure to describe a cipher test
 * @key:    A pointer to a key used by the test
 * @key_len:    The length of @key
 * @iv:     A pointer to the IV/Counter used by the test
 * @iv_len: The length of @iv
 * @input:  A pointer to data used as input
 * @ilen    The length of data in @input
 * @output: A pointer to what the test need to produce
 * @olen:   The length of data in @output
 * @algo:   The type of algorithm, refer to VIRTIO_CRYPTO_CIPHER_AES_*
 */
typedef struct VirtIOCryptoCipherTestData {
    unsigned short algo;
    const char *key;
    const char *iv;
    const char *input;
    const char *output;
    unsigned char key_len;
    unsigned char iv_len;
    unsigned short ilen;
    unsigned short olen;
} VirtIOCryptoCipherTestData;


static VirtIOCryptoCipherTestData cipher_test_data[] = {
    { /* From RFC 3602 */
        .algo = VIRTIO_CRYPTO_CIPHER_AES_CBC,
        .key  = "\x06\xa9\x21\x40\x36\xb8\xa1\x5b"
                "\x51\x2e\x03\xd5\x34\x12\x00\x06",
        .key_len   = 16,
        .iv = "\x3d\xaf\xba\x42\x9d\x9e\xb4\x30"
              "\xb4\x22\xda\x80\x2c\x9f\xac\x41",
        .iv_len = 16,
        .input  = "Single block msg",
        .ilen   = 16,
        .output = "\xe3\x53\x77\x9c\x10\x79\xae\xb8"
                  "\x27\x08\x94\x2d\xbe\x77\x18\x1a",
        .olen   = 16,
    },
};

static QPCIBus *virtio_crypto_test_start(void)
{
    char *cmdline;

    cmdline = g_strdup_printf(
               "-object cryptodev-backend-builtin,id=cryptodev0 "
               "-device virtio-crypto-pci,id=crypto0,"
               "cryptodev=cryptodev0");

    qtest_start(cmdline);
    g_free(cmdline);

    return qpci_init_pc();
}

static void test_end(void)
{
    qtest_end();
}

static QVirtioPCIDevice *virtio_crypto_pci_init(QPCIBus *bus, int slot)
{
    QVirtioPCIDevice *dev;

    dev = qvirtio_pci_device_find(bus, VIRTIO_ID_CRYPTO);
    g_assert(dev != NULL);
    g_assert_cmphex(dev->vdev.device_type, ==, VIRTIO_ID_CRYPTO);

    qvirtio_pci_device_enable(dev);
    qvirtio_reset(&qvirtio_pci, &dev->vdev);
    qvirtio_set_acknowledge(&qvirtio_pci, &dev->vdev);
    qvirtio_set_driver(&qvirtio_pci, &dev->vdev);

    return dev;
}

static uint64_t
virtio_crypto_ctrl_request(QGuestAllocator *alloc,
                           struct virtio_crypto_op_ctrl_req *req)
{
    uint64_t addr;

    addr = guest_alloc(alloc, sizeof(*req));

    memwrite(addr, req, sizeof(*req));

    return addr;
}

static uint64_t
virtio_crypto_data_request(QGuestAllocator *alloc,
                           struct virtio_crypto_op_data_req *req)
{
    uint64_t addr;

    addr = guest_alloc(alloc, sizeof(*req));

    memwrite(addr, req, sizeof(*req));

    return addr;
}

static void
virtio_crypto_driver_init(const QVirtioBus *bus, QVirtioDevice *dev)
{
    /* Read configure space to get  supported crypto services */

    qvirtio_set_driver_ok(bus, dev);
}

static uint64_t
virtio_crypto_create_session(const QVirtioBus *bus, QVirtioDevice *dev,
            QGuestAllocator *alloc, QVirtQueue *vq,
            VirtIOCryptoCipherTestData *data,
            int encrypt)
{
    uint32_t free_head;
    struct virtio_crypto_op_ctrl_req ctrl;
    struct virtio_crypto_session_input input;
    uint32_t key_len = data->key_len;
    uint64_t req_addr;
    uint64_t key_addr, input_addr; /* cipher key guest physical address */
    uint64_t session_id;
    QVRingIndirectDesc *indirect;

    /* Create an encryption session */
    ctrl.header.opcode = VIRTIO_CRYPTO_CIPHER_CREATE_SESSION;
    ctrl.header.algo = data->algo;
    /* Set the default dataqueue id to 0 */
    ctrl.header.queue_id = 0;

    /* Pad cipher's parameters */
    ctrl.u.sym_create_session.op_type = VIRTIO_CRYPTO_SYM_OP_CIPHER;
    ctrl.u.sym_create_session.u.cipher.para.algo = ctrl.header.algo;
    ctrl.u.sym_create_session.u.cipher.para.keylen = key_len;
    if (encrypt) {
        ctrl.u.sym_create_session.u.cipher.para.op = VIRTIO_CRYPTO_OP_ENCRYPT;
    } else {
        ctrl.u.sym_create_session.u.cipher.para.op = VIRTIO_CRYPTO_OP_DECRYPT;
    }

    req_addr = virtio_crypto_ctrl_request(alloc, &ctrl);

    /* Pad cipher's output data */
    key_addr = guest_alloc(alloc, key_len);
    memwrite(key_addr, data->key, key_len);

    input.status = VIRTIO_CRYPTO_ERR;
    input_addr = guest_alloc(alloc, sizeof(input));
    memwrite(input_addr, &input, sizeof(input));

    indirect = qvring_indirect_desc_setup(dev, alloc, 3);
    qvring_indirect_desc_add(indirect, req_addr, sizeof(ctrl), false);
    qvring_indirect_desc_add(indirect, key_addr, key_len, false);
    qvring_indirect_desc_add(indirect, input_addr, sizeof(input), true);
    free_head = qvirtqueue_add_indirect(vq, indirect);

    qvirtqueue_kick(bus, dev, vq, free_head);

    qvirtio_wait_queue_isr(bus, dev, vq, QVIRTIO_CRYPTO_TIMEOUT_US);

    /* calculate the offset of input data */

    memread(input_addr, &input, sizeof(input));

    /* Verify the result */
    g_assert_cmpint(input.status, ==, VIRTIO_CRYPTO_OK);

    session_id = input.session_id;

    g_free(indirect);
    guest_free(alloc, input_addr);
    guest_free(alloc, key_addr);
    guest_free(alloc, req_addr);

    return session_id;
}

static void
virtio_crypto_close_session(const QVirtioBus *bus, QVirtioDevice *dev,
            QGuestAllocator *alloc, QVirtQueue *vq,
            uint64_t session_id)
{
    uint32_t free_head;
    struct virtio_crypto_op_ctrl_req ctrl;
    uint64_t req_addr, status_addr;
    uint32_t status;
    QVRingIndirectDesc *indirect;

    /* Create an encryption session */
    ctrl.header.opcode = VIRTIO_CRYPTO_CIPHER_DESTROY_SESSION;
    /* Set the default dataqueue id to 0 */
    ctrl.header.queue_id = 0;

    ctrl.u.destroy_session.session_id = session_id;

    req_addr = virtio_crypto_ctrl_request(alloc, &ctrl);

    status_addr = guest_alloc(alloc, sizeof(status));
    writel(status_addr, VIRTIO_CRYPTO_ERR);

    indirect = qvring_indirect_desc_setup(dev, alloc, 2);
    qvring_indirect_desc_add(indirect, req_addr, sizeof(ctrl), false);
    qvring_indirect_desc_add(indirect, status_addr, sizeof(status), true);
    free_head = qvirtqueue_add_indirect(vq, indirect);

    qvirtqueue_kick(bus, dev, vq, free_head);

    qvirtio_wait_queue_isr(bus, dev, vq, QVIRTIO_CRYPTO_TIMEOUT_US);

    /* Verify the result */
    status = readl(status_addr);
    g_assert_cmpint(status, ==, VIRTIO_CRYPTO_OK);

    g_free(indirect);
    guest_free(alloc, req_addr);
    guest_free(alloc, status_addr);
}


static void
virtio_crypto_test_cipher(const QVirtioBus *bus, QVirtioDevice *dev,
            QGuestAllocator *alloc, QVirtQueue *ctrlq,
            QVirtQueue *vq, VirtIOCryptoCipherTestData *data,
            int encrypt)
{
    uint32_t free_head;
    struct virtio_crypto_op_data_req req;
    uint64_t req_addr, status_addr;
    uint64_t iv_addr, src_addr, dst_addr;
    uint64_t session_id;
    char *output;
    uint32_t src_len, dst_len, status;
    QVRingIndirectDesc *indirect;

    /* Create a session */
    session_id = virtio_crypto_create_session(bus, dev, alloc,
                                             ctrlq, data, encrypt);

    /* Head of operation */
    req.header.session_id = session_id;
    if (encrypt) {
        req.header.opcode = VIRTIO_CRYPTO_CIPHER_ENCRYPT;
    } else {
        req.header.opcode = VIRTIO_CRYPTO_CIPHER_DECRYPT;
    }

    req.u.sym_req.op_type = VIRTIO_CRYPTO_SYM_OP_CIPHER;
    req.u.sym_req.u.cipher.para.iv_len = data->iv_len;
    req.u.sym_req.u.cipher.para.src_data_len = data->ilen;
    req.u.sym_req.u.cipher.para.dst_data_len = data->olen;

    req_addr = virtio_crypto_data_request(alloc, &req);

    /* IV */
    if (data->iv_len > 0) {
        iv_addr = guest_alloc(alloc, data->iv_len);
        memwrite(iv_addr, data->iv, data->iv_len);
    }

    if (encrypt) {
        src_len = data->ilen;
        dst_len = data->olen;
        /* Source data is the input data which is a single buffer */
        src_addr = guest_alloc(alloc, src_len);
        memwrite(src_addr, data->input, src_len);
    } else {
        src_len = data->olen;
        dst_len = data->ilen;
        /* Source data is the output data which is a single buffer */
        src_addr = guest_alloc(alloc, src_len);
        memwrite(src_addr, data->output, src_len);
    }

    dst_addr = guest_alloc(alloc, dst_len);

    status_addr = guest_alloc(alloc, sizeof(status));
    writel(status_addr, VIRTIO_CRYPTO_ERR);

    indirect = qvring_indirect_desc_setup(dev, alloc, 5);
    qvring_indirect_desc_add(indirect, req_addr, sizeof(req), false);
    qvring_indirect_desc_add(indirect, iv_addr, data->iv_len, false);
    qvring_indirect_desc_add(indirect, src_addr, src_len, false);
    qvring_indirect_desc_add(indirect, dst_addr, dst_len, true);
    qvring_indirect_desc_add(indirect, status_addr, sizeof(status), true);
    free_head = qvirtqueue_add_indirect(vq, indirect);

    qvirtqueue_kick(bus, dev, vq, free_head);

    qvirtio_wait_queue_isr(bus, dev, vq, QVIRTIO_CRYPTO_TIMEOUT_US);

    /* Verify the result */
    status = readl(status_addr);
    g_assert_cmpint(status, ==, VIRTIO_CRYPTO_OK);

    output = g_malloc0(dst_len);
    memread(dst_addr, output, dst_len);
    if (encrypt) {
        g_assert_cmpstr(output, ==, data->output);
    } else {
        g_assert_cmpstr(output, ==, data->input);
    }
    g_free(output);

    g_free(indirect);

    if (data->iv_len > 0) {
        guest_free(alloc, iv_addr);
    }
    guest_free(alloc, src_addr);
    guest_free(alloc, dst_addr);
    guest_free(alloc, req_addr);
    guest_free(alloc, status_addr);

    /* Close the session */
    virtio_crypto_close_session(bus, dev, alloc, ctrlq, session_id);
}

static void virtio_crypto_pci_basic(void)
{
    QVirtioPCIDevice *dev;
    QPCIBus *bus;
    QGuestAllocator *alloc;
    QVirtQueuePCI *dataq, *controlq;
    size_t i;
    uint32_t features;

    bus = virtio_crypto_test_start();
    dev = virtio_crypto_pci_init(bus, PCI_SLOT);

    alloc = pc_alloc_init();

    features = qvirtio_get_features(&qvirtio_pci, &dev->vdev);
    g_assert_cmphex(features & (1u << VIRTIO_RING_F_INDIRECT_DESC), !=, 0);
    features = features & ~(QVIRTIO_F_BAD_FEATURE |
                            (1u << VIRTIO_RING_F_EVENT_IDX));
    qvirtio_set_features(&qvirtio_pci, &dev->vdev, features);

    dataq = (QVirtQueuePCI *)qvirtqueue_setup(&qvirtio_pci, &dev->vdev,
                                           alloc, 0);
    controlq = (QVirtQueuePCI *)qvirtqueue_setup(&qvirtio_pci, &dev->vdev,
                                           alloc, 1);

    virtio_crypto_driver_init(&qvirtio_pci, &dev->vdev);

    for (i = 0; i < G_N_ELEMENTS(cipher_test_data); i++) {
        /* Step 1: Encryption */
        virtio_crypto_test_cipher(&qvirtio_pci, &dev->vdev, alloc,
                                  &controlq->vq, &dataq->vq,
                                  &cipher_test_data[i], 1);
        /* Step 2: Decryption */
        virtio_crypto_test_cipher(&qvirtio_pci, &dev->vdev, alloc,
                                  &controlq->vq, &dataq->vq,
                                  &cipher_test_data[i], 0);
    }

    /* End test */
    qvirtqueue_cleanup(&qvirtio_pci, &dataq->vq, alloc);
    qvirtqueue_cleanup(&qvirtio_pci, &controlq->vq, alloc);
    pc_alloc_uninit(alloc);
    qvirtio_pci_device_disable(dev);
    g_free(dev);
    qpci_free_pc(bus);
    test_end();
}

int main(int argc, char **argv)
{
    const char *qemu;
    const char *arch;
    int ret;

    qemu = getenv("QTEST_QEMU_BINARY");
    if (qemu == NULL) {
        ret = setenv("QTEST_QEMU_BINARY",
                     "x86_64-softmmu/qemu-system-x86_64", 0);
        g_assert(ret == 0);
    }

    arch = qtest_get_arch();

    g_test_init(&argc, &argv, NULL);

    if (strcmp(arch, "i386") == 0 || strcmp(arch, "x86_64") == 0) {
        qtest_add_func("/virtio/crypto/pci/basic", virtio_crypto_pci_basic);
    }

    return g_test_run();
}
