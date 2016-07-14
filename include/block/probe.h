#ifndef PROBE_H
#define PROBE_H

int bochs_probe(const uint8_t *buf, int buf_size, const char *filename);
int cloop_probe(const uint8_t *buf, int buf_size, const char *filename);

#endif
