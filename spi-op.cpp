//
// Copyright (c) 2022 Chuanhong Guo <gch981213@gmail.com>
//

#include "spi-op.h"
#include "ch347.h"
#include <cstdlib>
#include <cstring>

struct ch347_priv *priv;

bool SPIDeviceInit() {
    int ret;
    priv = ch347_open();
    if (!priv)
        exit(-1);
    ret = ch347_setup_spi(priv, 3, false, false, false);
    if (ret)
        return false;
    int freq = 60000;
    ret = ch347_set_spi_freq(priv, &freq);
    return ret == 0;
}

void SPIDeviceRelease() {
    ch347_close(priv);
}


bool SPIWrite(const unsigned char *data, unsigned int size) {
    int ret;
    ch347_set_cs(priv, 0, 0);
    ret = ch347_spi_tx(priv, data, size);
    ch347_set_cs(priv, 0, 1);
    return ret == 0;
}

bool SPIWriteTwo(const unsigned char *in1, unsigned int in1_size, const unsigned char *in2, unsigned int in2_size) {
    int ret;
    ch347_set_cs(priv, 0, 0);
    ret = ch347_spi_tx(priv, in1, in1_size);
    if (ret)
        return false;
    ret = ch347_spi_tx(priv, in2, in2_size);
    ch347_set_cs(priv, 0, 1);
    return ret == 0;
}

bool SPIWriteThenRead(const unsigned char *in, unsigned int in_size, unsigned char *out, unsigned int out_size) {
    int ret;
    uint8_t buf[16];
    ch347_set_cs(priv, 0, 0);
    if (in_size + out_size <= sizeof(buf)) {
        memcpy(buf, in, in_size);
        memset(buf + in_size, 0, out_size);
        ret = ch347_spi_trx_full_duplex(priv, buf, in_size + out_size);
        memcpy(out, buf + in_size, out_size);
    } else {
        ret = ch347_spi_tx(priv, in, in_size);
        if (ret)
            return false;
        ret = ch347_spi_rx(priv, out, out_size);
    }
    ch347_set_cs(priv, 0, 1);
    return ret == 0;
}