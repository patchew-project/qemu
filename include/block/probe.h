#ifndef PROBE_H
#define PROBE_H

int vdi_probe(const uint8_t *buf, int buf_size, const char *filename);
int vhdx_probe(const uint8_t *buf, int buf_size, const char *filename);
int vmdk_probe(const uint8_t *buf, int buf_size, const char *filename);
int vpc_probe(const uint8_t *buf, int buf_size, const char *filename);
const char *bdrv_bochs_probe(const uint8_t *buf, int buf_size,
                             const char *filename, int *score);
const char *bdrv_cloop_probe(const uint8_t *buf, int buf_size,
                             const char *filename, int *score);
const char *bdrv_crypto_probe_luks(const uint8_t *buf, int buf_size,
                                   const char *filename, int *score);
const char *bdrv_dmg_probe(const uint8_t *buf, int buf_size,
                           const char *filename, int *score);
const char *bdrv_parallels_probe(const uint8_t *buf, int buf_size,
                                 const char *filename, int *score);
const char *bdrv_qcow_probe(const uint8_t *buf, int buf_size,
                            const char *filename, int *score);
const char *bdrv_qcow2_probe(const uint8_t *buf, int buf_size,
                             const char *filename, int *score);
const char *bdrv_qed_probe(const uint8_t *buf, int buf_size,
                           const char *filename, int *score);
const char *bdrv_raw_probe(const uint8_t *buf, int buf_size,
                           const char *filename, int *score);

#endif
