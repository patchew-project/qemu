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
#include "qapi/qapi-types-machine-s390x.h"

static BootCertificateList *s390_get_boot_certs(void)
{
    return S390_CCW_MACHINE(qdev_get_machine())->boot_certs;
}

static size_t cert2buf(char *path, char **cert_buf)
{
    size_t size;

    if (!g_file_get_contents(path, cert_buf, &size, NULL)) {
        return 0;
    }

    return size;
}

static S390IPLCertificate *init_cert_x509(size_t size, uint8_t *raw, Error **errp)
{
    S390IPLCertificate *cert = NULL;
    g_autofree uint8_t *cert_der = NULL;
    size_t der_len = size;
    int rc;

    rc = qcrypto_x509_convert_cert_der(raw, size, &cert_der, &der_len, errp);
    if (rc != 0) {
        return NULL;
    }

    cert = g_new0(S390IPLCertificate, 1);
    cert->size = size;
    cert->der_size = der_len;
    /* store raw pointer - ownership transfers to cert */
    cert->raw = raw;

    return cert;
}

static S390IPLCertificate *init_cert(char *path)
{
    char *buf;
    size_t size;
    char vc_name[VC_NAME_LEN_BYTES];
    g_autofree gchar *filename = NULL;
    S390IPLCertificate *cert = NULL;
    Error *local_err = NULL;

    filename = g_path_get_basename(path);

    size = cert2buf(path, &buf);
    if (size == 0) {
        error_report("Failed to load certificate: %s", path);
        return NULL;
    }

    cert = init_cert_x509(size, (uint8_t *)buf, &local_err);
    if (cert == NULL) {
        error_reportf_err(local_err, "Failed to initialize certificate: %s:  ", path);
        g_free(buf);
        return NULL;
    }

    /*
     * Left justified certificate name with padding on the right with blanks.
     * Convert certificate name to EBCDIC.
     */
    strpadcpy(vc_name, VC_NAME_LEN_BYTES, filename, ' ');
    ebcdic_put(cert->vc_name, vc_name, VC_NAME_LEN_BYTES);

    return cert;
}

static void update_cert_store(S390IPLCertificateStore *cert_store,
                              S390IPLCertificate *cert)
{
    size_t data_buf_size;
    size_t keyid_buf_size;
    size_t hash_buf_size;
    size_t cert_buf_size;

    /* length field is word aligned for later DIAG use */
    keyid_buf_size = ROUND_UP(CERT_KEY_ID_LEN, 4);
    hash_buf_size = ROUND_UP(CERT_HASH_LEN, 4);
    cert_buf_size = ROUND_UP(cert->der_size, 4);
    data_buf_size = keyid_buf_size + hash_buf_size + cert_buf_size;

    if (cert_store->max_cert_size < data_buf_size) {
        cert_store->max_cert_size = data_buf_size;
    }

    cert_store->certs[cert_store->count] = *cert;
    cert_store->total_bytes += data_buf_size;
    cert_store->count++;
}

static GPtrArray *get_cert_paths(void)
{
    BootCertificateList *path_list = NULL;
    BootCertificateList *list = NULL;
    gchar *cert_path;
    GDir *dir = NULL;
    const gchar *filename;
    g_autoptr(GError) err = NULL;
    g_autoptr(GPtrArray) cert_path_builder = g_ptr_array_new_full(0, g_free);

    path_list = s390_get_boot_certs();
    if (path_list == NULL) {
        return g_steal_pointer(&cert_path_builder);
    }

    for (list = path_list; list; list = list->next) {
        cert_path = list->value->path;

        if (g_strcmp0(cert_path, "") == 0) {
            error_report("Empty path in certificate path list is not allowed");
            goto fail;
        }

        struct stat st;
        if (stat(cert_path, &st) != 0) {
            error_report("Failed to stat path '%s': %s", cert_path, g_strerror(errno));
            goto fail;
        }

        if (S_ISREG(st.st_mode)) {
            if (!g_str_has_suffix(cert_path, ".pem")) {
                error_report("Certificate file '%s' must have a .pem extension",
                             cert_path);
                goto fail;
            }

            g_ptr_array_add(cert_path_builder, g_strdup(cert_path));
        } else if (S_ISDIR(st.st_mode)) {
            dir = g_dir_open(cert_path, 0, &err);
            if (dir == NULL) {
                error_report("Failed to open directory '%s': %s",
                             cert_path, err->message);
                goto fail;
            }

            while ((filename = g_dir_read_name(dir))) {
                if (g_str_has_suffix(filename, ".pem")) {
                    g_ptr_array_add(cert_path_builder,
                                    g_build_filename(cert_path, filename, NULL));
                }
            }

            g_dir_close(dir);
        } else {
            error_report("Path '%s' is neither a file nor a directory", cert_path);
            goto fail;
        }
    }

    qapi_free_BootCertificateList(path_list);
    return g_steal_pointer(&cert_path_builder);

fail:
    qapi_free_BootCertificateList(path_list);
    exit(1);
}

void s390_ipl_create_cert_store(S390IPLCertificateStore *cert_store)
{
    GPtrArray *cert_path_builder;

    cert_path_builder = get_cert_paths();
    if (cert_path_builder->len == 0) {
        g_ptr_array_free(cert_path_builder, TRUE);
        return;
    }

    if (cert_path_builder->len > MAX_CERTIFICATES - 1) {
        error_report("Cert store exceeds maximum of %d certificates", MAX_CERTIFICATES);
        g_ptr_array_free(cert_path_builder, TRUE);
        exit(1);
    }

    cert_store->max_cert_size = 0;
    cert_store->total_bytes = 0;

    for (int i = 0; i < cert_path_builder->len; i++) {
        S390IPLCertificate *cert = init_cert((char *) cert_path_builder->pdata[i]);
        if (!cert) {
            g_ptr_array_free(cert_path_builder, TRUE);
            exit(1);
        }

        update_cert_store(cert_store, cert);
    }

    g_ptr_array_free(cert_path_builder, TRUE);
}
