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
    const char *path;
    unsigned short algo;
    const char *key;
    const char *iv;
    const char *input;
    const char *output;
    unsigned char key_len;
    unsigned char iv_len;
    unsigned short ilen;
    unsigned short olen;
    bool is_statelss_mode;
} VirtIOCryptoCipherTestData;


static VirtIOCryptoCipherTestData cipher_test_data[] = {
    { /* From RFC 3602 */
        .path = "/virtio/crypto/cbc(aes-128-session-mode)",
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
        .is_statelss_mode = false,
    },
    { /* From RFC 3602 */
        .path = "/virtio/crypto/cbc(aes-128-stateless-mode)",
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
        .is_statelss_mode = true,
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

    return qpci_init_pc(NULL);
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
    qvirtio_reset(&dev->vdev);
    qvirtio_set_acknowledge(&dev->vdev);
    qvirtio_set_driver(&dev->vdev);

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
virtio_crypto_driver_init(QVirtioDevice *dev)
{
    /* Read configure space to get  supported crypto services */

    qvirtio_set_driver_ok(dev);
}

static uint64_t
virtio_crypto_create_session(QVirtioDevice *dev,
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

    qvirtqueue_kick(dev, vq, free_head);

    qvirtio_wait_queue_isr(dev, vq, QVIRTIO_CRYPTO_TIMEOUT_US);

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
virtio_crypto_close_session(QVirtioDevice *dev,
            QGuestAllocator *alloc, QVirtQueue *vq,
            uint64_t session_id)
{
    uint32_t free_head;
    struct virtio_crypto_op_ctrl_req ctrl;
    uint64_t req_addr, status_addr;
    uint8_t status;
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

    qvirtqueue_kick(dev, vq, free_head);

    qvirtio_wait_queue_isr(dev, vq, QVIRTIO_CRYPTO_TIMEOUT_US);

    /* Verify the result */
    status = readl(status_addr);
    g_assert_cmpint(status, ==, VIRTIO_CRYPTO_OK);

    g_free(indirect);
    guest_free(alloc, req_addr);
    guest_free(alloc, status_addr);
}


static void
virtio_crypto_test_cipher_session_mode(QVirtioDevice *dev,
            QGuestAllocator *alloc, QVirtQueue *ctrlq,
            QVirtQueue *vq, VirtIOCryptoCipherTestData *data,
            int encrypt)
{
    uint32_t free_head;
    struct virtio_crypto_op_data_req req;
    uint64_t req_addr, status_addr;
    uint64_t iv_addr = 0, src_addr, dst_addr;
    uint64_t session_id;
    char *output;
    uint32_t src_len, dst_len;
    uint8_t status;
    QVRingIndirectDesc *indirect;
    uint8_t entry_num;

    /* Create a session */
    session_id = virtio_crypto_create_session(dev, alloc,
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

        /* header + iv + src + dst + status */
        entry_num = 5;
    } else {
        /* header + src + dst + status */
        entry_num = 4;
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

    /* Allocate descripto table entries */
    indirect = qvring_indirect_desc_setup(dev, alloc, entry_num);
    qvring_indirect_desc_add(indirect, req_addr, sizeof(req), false);
    if (data->iv_len > 0) {
        qvring_indirect_desc_add(indirect, iv_addr, data->iv_len, false);
    }
    qvring_indirect_desc_add(indirect, src_addr, src_len, false);
    qvring_indirect_desc_add(indirect, dst_addr, dst_len, true);
    qvring_indirect_desc_add(indirect, status_addr, sizeof(status), true);
    free_head = qvirtqueue_add_indirect(vq, indirect);

    qvirtqueue_kick(dev, vq, free_head);

    qvirtio_wait_queue_isr(dev, vq, QVIRTIO_CRYPTO_TIMEOUT_US);

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
    virtio_crypto_close_session(dev, alloc, ctrlq, session_id);
}

static void
virtio_crypto_test_cipher_stateless_mode(QVirtioDevice *dev,
            QGuestAllocator *alloc,
            QVirtQueue *vq, VirtIOCryptoCipherTestData *data,
            int encrypt)
{
    uint32_t free_head;
    struct virtio_crypto_op_data_req_mux req;
    uint64_t req_addr, status_addr;
    uint64_t iv_addr = 0, src_addr, dst_addr, key_addr;
    char *output;
    uint32_t src_len, dst_len;
    uint8_t status;
    QVRingIndirectDesc *indirect;
    uint8_t entry_num;

    /* Head of operation */
    req.header.flag = VIRTIO_CRYPTO_FLAG_STATELESS_MODE;
    if (encrypt) {
        req.header.opcode = VIRTIO_CRYPTO_CIPHER_ENCRYPT;
        req.u.sym_stateless_req.u.cipher.para.sess_para.op =
            VIRTIO_CRYPTO_OP_ENCRYPT;
    } else {
        req.header.opcode = VIRTIO_CRYPTO_CIPHER_DECRYPT;
        req.u.sym_stateless_req.u.cipher.para.sess_para.op =
            VIRTIO_CRYPTO_OP_DECRYPT;
    }

    req.u.sym_stateless_req.op_type = VIRTIO_CRYPTO_SYM_OP_CIPHER;
    req.u.sym_stateless_req.u.cipher.para.sess_para.algo = data->algo;
    req.u.sym_stateless_req.u.cipher.para.sess_para.keylen = data->key_len;
    req.u.sym_stateless_req.u.cipher.para.iv_len = data->iv_len;
    req.u.sym_stateless_req.u.cipher.para.src_data_len = data->ilen;
    req.u.sym_stateless_req.u.cipher.para.dst_data_len = data->olen;

    req_addr = guest_alloc(alloc, sizeof(req));
    memwrite(req_addr, &req, sizeof(req));

    g_assert(data->key_len > 0);
    key_addr = guest_alloc(alloc, data->key_len);
    memwrite(key_addr, data->key, data->key_len);

    /* IV */
    if (data->iv_len > 0) {
        iv_addr = guest_alloc(alloc, data->iv_len);
        memwrite(iv_addr, data->iv, data->iv_len);

        /* header + key + iv + src + dst + status */
        entry_num = 6;
    } else {
        /* header + key + src + dst + status */
        entry_num = 5;
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

    /* Allocate desc table entries */
    indirect = qvring_indirect_desc_setup(dev, alloc, entry_num);
    qvring_indirect_desc_add(indirect, req_addr, sizeof(req), false);
    qvring_indirect_desc_add(indirect, key_addr, data->key_len, false);
    if (data->iv_len > 0) {
        qvring_indirect_desc_add(indirect, iv_addr, data->iv_len, false);
    }
    qvring_indirect_desc_add(indirect, src_addr, src_len, false);
    qvring_indirect_desc_add(indirect, dst_addr, dst_len, true);
    qvring_indirect_desc_add(indirect, status_addr, sizeof(status), true);
    free_head = qvirtqueue_add_indirect(vq, indirect);

    qvirtqueue_kick(dev, vq, free_head);

    qvirtio_wait_queue_isr(dev, vq, QVIRTIO_CRYPTO_TIMEOUT_US);

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
    guest_free(alloc, key_addr);
    if (data->iv_len > 0) {
        guest_free(alloc, iv_addr);
    }
    guest_free(alloc, src_addr);
    guest_free(alloc, dst_addr);
    guest_free(alloc, req_addr);
    guest_free(alloc, status_addr);
}

static void
virtio_crypto_test_cipher(QVirtioDevice *dev,
            QGuestAllocator *alloc, QVirtQueue *ctrlq,
            QVirtQueue *dataq, VirtIOCryptoCipherTestData *data,
            int encrypt)
{
    if (!data->is_statelss_mode) {
        virtio_crypto_test_cipher_session_mode(dev, alloc,
            ctrlq, dataq, data, encrypt);
    } else {
        virtio_crypto_test_cipher_stateless_mode(dev, alloc,
            dataq, data, encrypt);
    }
}

static void virtio_crypto_pci_basic(void *opaque)
{
    VirtIOCryptoCipherTestData *test_data = opaque;
    QVirtioPCIDevice *dev;
    QPCIBus *bus;
    QGuestAllocator *alloc;
    QVirtQueuePCI *dataq, *controlq;
    uint32_t features;

    bus = virtio_crypto_test_start();
    dev = virtio_crypto_pci_init(bus, PCI_SLOT);

    alloc = pc_alloc_init();

    features = qvirtio_get_features(&dev->vdev);
    g_assert_cmphex(features & (1u << VIRTIO_RING_F_INDIRECT_DESC), !=, 0);

    if (!test_data->is_statelss_mode) {
        features = features & ~(QVIRTIO_F_BAD_FEATURE |
                                (1u << VIRTIO_RING_F_EVENT_IDX |
                                1u << VIRTIO_CRYPTO_F_MUX_MODE |
                                1u << VIRTIO_CRYPTO_F_CIPHER_STATELESS_MODE));
    } else {
        features = features & ~(QVIRTIO_F_BAD_FEATURE |
                                (1u << VIRTIO_RING_F_EVENT_IDX));
    }
    qvirtio_set_features(&dev->vdev, features);

    dataq = (QVirtQueuePCI *)qvirtqueue_setup(&dev->vdev,
                                           alloc, 0);
    controlq = (QVirtQueuePCI *)qvirtqueue_setup(&dev->vdev,
                                           alloc, 1);

    virtio_crypto_driver_init(&dev->vdev);

    /* Step 1: Encryption */
    virtio_crypto_test_cipher(&dev->vdev, alloc,
                              &controlq->vq, &dataq->vq,
                              test_data, 1);
    /* Step 2: Decryption */
    virtio_crypto_test_cipher(&dev->vdev, alloc,
                              &controlq->vq, &dataq->vq,
                              test_data, 0);

    /* End test */
    guest_free(alloc, dataq->vq.desc);
    guest_free(alloc, controlq->vq.desc);
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
    int i, ret;

    qemu = getenv("QTEST_QEMU_BINARY");
    if (qemu == NULL) {
        ret = setenv("QTEST_QEMU_BINARY",
                     "x86_64-softmmu/qemu-system-x86_64", 0);
        g_assert(ret == 0);
    }

    arch = qtest_get_arch();

    g_test_init(&argc, &argv, NULL);

    if (strcmp(arch, "i386") == 0 || strcmp(arch, "x86_64") == 0) {
        for (i = 0; i < G_N_ELEMENTS(cipher_test_data); i++) {
            g_test_add_data_func(cipher_test_data[i].path,
                                 (void *)&cipher_test_data[i],
                                 (GTestDataFunc)virtio_crypto_pci_basic);
        }
    }

    return g_test_run();
}
