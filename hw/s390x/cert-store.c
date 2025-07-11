/*
 * S390 certificate store implementation
 *
 * Copyright 2025 IBM Corp.
 * Author(s): Zhuoying Cai <zycai@linux.ibm.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "cert-store.h"
#include "qapi/error.h"
#include "qemu/error-report.h"
#include "qemu/option.h"
#include "qemu/config-file.h"
#include "hw/s390x/ebcdic.h"
#include "hw/s390x/s390-virtio-ccw.h"
#include "qemu/cutils.h"
#include "crypto/x509-utils.h"

static const char *s390_get_boot_certificates(void)
{
    return S390_CCW_MACHINE(qdev_get_machine())->boot_certificates;
}

static size_t cert2buf(char *path, char **cert_buf)
{
    size_t size;

    if (!g_file_get_contents(path, cert_buf, &size, NULL) || size == 0) {
        return 0;
    }

    return size;
}

static S390IPLCertificate *init_cert_x509(size_t size, uint8_t *raw)
{
    S390IPLCertificate *q_cert = NULL;
    int key_id_size;
    int hash_size;
    int hash_type;
    Error *err = NULL;

    g_autofree uint8_t *cert_der = NULL;
    size_t der_len = size;
    int rc;

    hash_type = qcrypto_x509_get_signature_algorithm(raw, size, &err);
    if (hash_type == -1) {
        error_report_err(err);
        return NULL;
    }
    if (hash_type != QCRYPTO_SIG_ALGO_RSA_SHA256) {
        error_report_err(err);
        return NULL;
    }

    key_id_size = qcrypto_x509_get_keyid_len(QCRYPTO_KEYID_FLAGS_SHA256, &err);
    if (key_id_size == -1) {
        error_report_err(err);
        return NULL;
    }

    hash_size = QCRYPTO_HASH_DIGEST_LEN_SHA256;

    rc = qcrypto_x509_convert_cert_der(raw, size, &cert_der, &der_len, &err);
    if (rc != 0) {
        error_report_err(err);
        return NULL;
    }

    q_cert = g_new0(S390IPLCertificate, 1);
    q_cert->size = size;
    q_cert->der_size = der_len;
    q_cert->key_id_size = key_id_size;
    q_cert->hash_size = hash_size;
    q_cert->raw = raw;
    q_cert->hash_type = QCRYPTO_SIG_ALGO_RSA_SHA256;

    return q_cert;
}

static S390IPLCertificate *init_cert(char *path)
{
    char *buf;
    size_t size;
    char vc_name[VC_NAME_LEN_BYTES];
    g_autofree gchar *filename = NULL;
    S390IPLCertificate *qcert = NULL;

    filename = g_path_get_basename(path);

    size = cert2buf(path, &buf);
    if (size == 0) {
        error_report("Failed to load certificate: %s", path);
        g_free(buf);
        return NULL;
    }

    qcert = init_cert_x509(size, (uint8_t *)buf);
    if (qcert == NULL) {
        error_report("Failed to initialize certificate: %s", path);
        g_free(buf);
        return NULL;
    }

    /*
     * Left justified certificate name with padding on the right with blanks.
     * Convert certificate name to EBCDIC.
     */
    strpadcpy(vc_name, VC_NAME_LEN_BYTES, filename, ' ');
    ebcdic_put(qcert->vc_name, vc_name, VC_NAME_LEN_BYTES);

    return qcert;
}

static void update_cert_store(S390IPLCertificateStore *cert_store,
                              S390IPLCertificate *qcert)
{
    size_t data_buf_size;
    size_t keyid_buf_size;
    size_t hash_buf_size;
    size_t cert_buf_size;

    /* length field is word aligned for later DIAG use */
    keyid_buf_size = ROUND_UP(qcert->key_id_size, 4);
    hash_buf_size = ROUND_UP(qcert->hash_size, 4);
    cert_buf_size = ROUND_UP(qcert->der_size, 4);
    data_buf_size = keyid_buf_size + hash_buf_size + cert_buf_size;

    if (cert_store->max_cert_size < data_buf_size) {
        cert_store->max_cert_size = data_buf_size;
    }

    cert_store->certs[cert_store->count] = *qcert;
    cert_store->total_bytes += data_buf_size;
    cert_store->count++;
}

static GPtrArray *get_cert_paths(void)
{
    const char *paths_list;
    g_auto(GStrv) paths = NULL;
    gchar **paths_copy;
    GDir *dir = NULL;
    const gchar *filename;
    g_autoptr(GError) err = NULL;
    g_autoptr(GPtrArray) cert_path_builder = g_ptr_array_new_full(0, g_free);

    paths_list = s390_get_boot_certificates();
    if (paths_list == NULL) {
        return g_steal_pointer(&cert_path_builder);
    }

    paths = g_strsplit(paths_list, ":", -1);

    paths_copy = paths;
    while (*paths_copy) {
        if (!strcmp(*paths_copy, "")) {
            error_report("Empty path in certificate path list is not allowed");
            exit(1);
        }

        struct stat st;
        if (stat(*paths_copy, &st) != 0) {
            error_report("Failed to stat path '%s': %s", *paths_copy, g_strerror(errno));
            exit(1);
        }

        if (S_ISREG(st.st_mode)) {
            g_ptr_array_add(cert_path_builder, g_strdup(*paths_copy));
        } else if (S_ISDIR(st.st_mode)) {
            dir = g_dir_open(*paths_copy, 0, &err);
            if (dir == NULL) {
                error_report("Failed to open directory '%s': %s",
                             *paths_copy, err->message);
                exit(1);
            }

            while ((filename = g_dir_read_name(dir))) {
                g_ptr_array_add(cert_path_builder,
                                g_build_filename(*paths_copy, filename, NULL));
            }

            g_dir_close(dir);
        } else {
            error_report("Path '%s' is neither a file nor a directory", *paths_copy);
        }

        paths_copy += 1;
    }

    return g_steal_pointer(&cert_path_builder);
}

void s390_ipl_create_cert_store(S390IPLCertificateStore *cert_store)
{
    GPtrArray *cert_path_builder;

    cert_path_builder = get_cert_paths();
    if (cert_path_builder->len == 0) {
        g_ptr_array_free(cert_path_builder, TRUE);
        return;
    }

    cert_store->max_cert_size = 0;
    cert_store->total_bytes = 0;

    for (int i = 0; i < cert_path_builder->len; i++) {
        if (i > MAX_CERTIFICATES - 1) {
            error_report("Maximum %d certificates are allowed", MAX_CERTIFICATES);
            exit(1);
        }

        S390IPLCertificate *qcert = init_cert((char *) cert_path_builder->pdata[i]);
        if (qcert) {
            update_cert_store(cert_store, qcert);
        }
    }

    g_ptr_array_free(cert_path_builder, TRUE);
}
