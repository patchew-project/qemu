#ifndef PROBE_H
#define PROBE_H

int block_crypto_probe_luks(const uint8_t *buf, int buf_size,
                            const char *filename);
int dmg_probe(const uint8_t *buf, int buf_size, const char *filename);
int parallels_probe(const uint8_t *buf, int buf_size, const char *filename);
int qcow_probe(const uint8_t *buf, int buf_size, const char *filename);
int qcow2_probe(const uint8_t *buf, int buf_size, const char *filename);
int bdrv_qed_probe(const uint8_t *buf, int buf_size, const char *filename);
int raw_probe(const uint8_t *buf, int buf_size, const char *filename);
int vdi_probe(const uint8_t *buf, int buf_size, const char *filename);
int vhdx_probe(const uint8_t *buf, int buf_size, const char *filename);
int vmdk_probe(const uint8_t *buf, int buf_size, const char *filename);
int vpc_probe(const uint8_t *buf, int buf_size, const char *filename);
const char *bdrv_bochs_probe(const uint8_t *buf, int buf_size,
                             const char *filename, int *score);
const char *bdrv_cloop_probe(const uint8_t *buf, int buf_size,
                             const char *filename, int *score);

#endif
