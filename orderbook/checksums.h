/*
Copyright (C) 2020-2021  Bryant Moscon - bmoscon@gmail.com

Please see the LICENSE file for the terms and conditions
associated with this software.
*/
#ifndef __CHECKSUMS__
#define __CHECKSUMS__

#include <stdint.h>
#include <stddef.h>
#include "orderbook.h"


enum Checksums {
    KRAKEN,
    INVALID_CHECKSUM_FORMAT
};


PyObject* calculate_checksum(Orderbook *ob);
PyObject* kraken_checksum(Orderbook *ob);

uint32_t crc32(const unsigned char *data, size_t len);

#endif