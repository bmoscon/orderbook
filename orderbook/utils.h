/*
Copyright (C) 2020-2026  Bryant Moscon - bmoscon@gmail.com

Please see the LICENSE file for the terms and conditions
associated with this software.
*/
#ifndef __UTILS__
#define __UTILS__

#include <stdint.h>
#include <stddef.h>

#define EXPECT(EXPR, VAL) __builtin_expect((EXPR), (VAL))


enum side_e {
    BID,
    ASK,
    INVALID_SIDE
};


enum side_e check_key(const char *key);
int crc32_orderbook_init(void);
uint32_t crc32_orderbook(const uint8_t *data, size_t len);

#endif
