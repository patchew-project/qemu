#include "qemu/osdep.h"
#include "qapi-types.h"
#include "block/probe.h"
#include "crypto/block.h"

static int block_crypto_probe_generic(QCryptoBlockFormat format,
                                      const uint8_t *buf,
                                      int buf_size,
                                      const char *filename)
{
    if (qcrypto_block_has_format(format, buf, buf_size)) {
        return 100;
    } else {
        return 0;
    }
}

const char *bdrv_crypto_probe_luks(const uint8_t *buf, int buf_size,
                                   const char *filename, int *score)
{
    const char *format = "luks";
    assert(score);
    *score = block_crypto_probe_generic(Q_CRYPTO_BLOCK_FORMAT_LUKS,
                                        buf, buf_size, filename);
    return format;
}
