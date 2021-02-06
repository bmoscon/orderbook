/*
Copyright (C) 2020-2021  Bryant Moscon - bmoscon@gmail.com

Please see the LICENSE file for the terms and conditions
associated with this software.
*/
#include <string.h>
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
CRC source code based on:
https://stackoverflow.com/questions/302914/crc32-c-or-c-implementation

This code is not subject to the license of this software.

Author states CRC32 source code is free to use for all purposes, including commercial.
*/
uint32_t crc32_table(const uint8_t *data, size_t len)
{
      uint32_t crc = 0xFFFFFFFF;

      while (len--) {
          crc = crc_32_table[((crc) ^ (*data++)) & 0xFF] ^ ((crc) >> 8);
      }

      return ~crc;
}
