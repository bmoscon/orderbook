/*
Copyright (C) 2020-2021  Bryant Moscon - bmoscon@gmail.com

Please see the LICENSE file for the terms and conditions
associated with this software.
*/
#include <stdio.h>
#include <string.h>
#include <stddef.h>
#include <stdint.h>
#include "utils.h"


enum side_e check_key(const char *key)
{
    if (!strcmp(key, "bid") || !strcmp(key, "BID") || !strcmp(key, "bids") || !strcmp(key, "BIDS")) {
        return BID;
    }

    if (!strcmp(key, "ask") || !strcmp(key, "ASK") || !strcmp(key, "asks") || !strcmp(key, "ASKS")) {
        return ASK;
    }

    return INVALID_SIDE;
}

/*
 * CRC32 implementation based on https://stackoverflow.com/questions/27939882/fast-crc-algorithm
 *
 *  This function's code is not subject to the license of this software
 */
uint32_t crc32(const uint8_t *data, size_t len)
{
    int k;
    uint32_t checksum = ~0;

    while (len--) {
        checksum ^= *data++;
        for (k = 0; k < 8; k++) {
            checksum = checksum & 1 ? (checksum >> 1) ^ 0xEDB88320 : checksum >> 1;
        }
    }

    return ~checksum;
}
