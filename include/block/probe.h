#ifndef PROBE_H
#define PROBE_H

int bochs_probe(const uint8_t *buf, int buf_size, const char *filename);
int cloop_probe(const uint8_t *buf, int buf_size, const char *filename);
int block_crypto_probe_luks(const uint8_t *buf, int buf_size,
                            const char *filename);
int dmg_probe(const uint8_t *buf, int buf_size, const char *filename);
int parallels_probe(const uint8_t *buf, int buf_size, const char *filename);

#endif
