/*
Copyright (C) 2020-2026  Bryant Moscon - bmoscon@gmail.com

Please see the LICENSE file for the terms and conditions
associated with this software.
*/
#include <string.h>
#include <stdint.h>
#include "utils.h"


enum side_e check_key(const char *key)
{
    switch (key[0]) {
        case 'b':
            return (!strcmp(key + 1, "id") || !strcmp(key + 1, "ids")) ? BID : INVALID_SIDE;
        case 'B':
            return (!strcmp(key + 1, "ID") || !strcmp(key + 1, "IDS")) ? BID : INVALID_SIDE;
        case 'a':
            return (!strcmp(key + 1, "sk") || !strcmp(key + 1, "sks")) ? ASK : INVALID_SIDE;
        case 'A':
            return (!strcmp(key + 1, "SK") || !strcmp(key + 1, "SKS")) ? ASK : INVALID_SIDE;
        default:
            return INVALID_SIDE;
    }
}


/*
CRC checksums for
  * arm64 with CRC32 extension
  * arm64 without extension
  * x86-64 with PCLMULQDQ instruction
  * failure case. The above should cover most any hardware made within the last 10 years
    note: crc32_orderbook_init will return -1 when this is the case
*/

#if defined(__ARM_FEATURE_CRC32)
#include <arm_acle.h>

int crc32_orderbook_init(void)
{
    return 0;
}

uint32_t crc32_orderbook(const uint8_t *data, size_t len)
{
    uint32_t crc = 0xFFFFFFFF;

    while (len >= 8) {
        uint64_t chunk;
        memcpy(&chunk, data, 8);
        crc = __crc32d(crc, chunk);
        data += 8;
        len -= 8;
    }

    while (len--) {
        crc = __crc32b(crc, *data++);
    }

    return ~crc;
}

#elif defined(__aarch64__)
#include <arm_acle.h>
#if defined(__linux__)
#include <sys/auxv.h>
#include <asm/hwcap.h>
#endif

int crc32_orderbook_init(void)
{
#if defined(__linux__) && defined(HWCAP_CRC32)
    if (!(getauxval(AT_HWCAP) & HWCAP_CRC32)) {
        return -1;
    }
#endif
    return 0;
}

#if defined(__clang__)
__attribute__((target("crc")))
#else
__attribute__((target("+crc")))
#endif
uint32_t crc32_orderbook(const uint8_t *data, size_t len)
{
    uint32_t crc = 0xFFFFFFFF;

    while (len >= 8) {
        uint64_t chunk;
        memcpy(&chunk, data, 8);
        crc = __crc32d(crc, chunk);
        data += 8;
        len -= 8;
    }

    while (len--) {
        crc = __crc32b(crc, *data++);
    }

    return ~crc;
}

#elif defined(__x86_64__)
// PCLMULQDQ on any intel chip since 2010 (some atom processors may be later years)
// part of SSE4.1
#include <immintrin.h>

int crc32_orderbook_init(void)
{
    return (__builtin_cpu_supports("pclmul") && __builtin_cpu_supports("sse4.1")) ? 0 : -1;
}

__attribute__((target("pclmul,sse4.1")))
static uint32_t crc32_fold_pclmul(const uint8_t *data, size_t len, uint32_t crc)
{
    const __m128i k1k2 = _mm_set_epi64x(0x01c6e41596, 0x0154442bd4);
    const __m128i k3k4 = _mm_set_epi64x(0x00ccaa009e, 0x01751997d0);
    const __m128i k5 = _mm_set_epi64x(0, 0x0163cd6124);
    const __m128i poly_mu = _mm_set_epi64x(0x01f7011641, 0x01db710641);
    const __m128i mask32 = _mm_setr_epi32(-1, 0, -1, 0);

    __m128i x1 = _mm_loadu_si128((const __m128i *)(data + 0));
    __m128i x2 = _mm_loadu_si128((const __m128i *)(data + 16));
    __m128i x3 = _mm_loadu_si128((const __m128i *)(data + 32));
    __m128i x4 = _mm_loadu_si128((const __m128i *)(data + 48));
    __m128i t;

    x1 = _mm_xor_si128(x1, _mm_cvtsi32_si128((int)crc));
    data += 64;
    len -= 64;

    while (len >= 64) {
        t  = _mm_clmulepi64_si128(x1, k1k2, 0x00);
        x1 = _mm_clmulepi64_si128(x1, k1k2, 0x11);
        x1 = _mm_xor_si128(_mm_xor_si128(x1, t), _mm_loadu_si128((const __m128i *)(data + 0)));

        t  = _mm_clmulepi64_si128(x2, k1k2, 0x00);
        x2 = _mm_clmulepi64_si128(x2, k1k2, 0x11);
        x2 = _mm_xor_si128(_mm_xor_si128(x2, t), _mm_loadu_si128((const __m128i *)(data + 16)));

        t  = _mm_clmulepi64_si128(x3, k1k2, 0x00);
        x3 = _mm_clmulepi64_si128(x3, k1k2, 0x11);
        x3 = _mm_xor_si128(_mm_xor_si128(x3, t), _mm_loadu_si128((const __m128i *)(data + 32)));

        t  = _mm_clmulepi64_si128(x4, k1k2, 0x00);
        x4 = _mm_clmulepi64_si128(x4, k1k2, 0x11);
        x4 = _mm_xor_si128(_mm_xor_si128(x4, t), _mm_loadu_si128((const __m128i *)(data + 48)));

        data += 64;
        len -= 64;
    }

    // fold four lanes to one
    t  = _mm_clmulepi64_si128(x1, k3k4, 0x00);
    x1 = _mm_clmulepi64_si128(x1, k3k4, 0x11);
    x1 = _mm_xor_si128(_mm_xor_si128(x1, t), x2);

    t  = _mm_clmulepi64_si128(x1, k3k4, 0x00);
    x1 = _mm_clmulepi64_si128(x1, k3k4, 0x11);
    x1 = _mm_xor_si128(_mm_xor_si128(x1, t), x3);

    t  = _mm_clmulepi64_si128(x1, k3k4, 0x00);
    x1 = _mm_clmulepi64_si128(x1, k3k4, 0x11);
    x1 = _mm_xor_si128(_mm_xor_si128(x1, t), x4);

    while (len >= 16) {
        t  = _mm_clmulepi64_si128(x1, k3k4, 0x00);
        x1 = _mm_clmulepi64_si128(x1, k3k4, 0x11);
        x1 = _mm_xor_si128(_mm_xor_si128(x1, t), _mm_loadu_si128((const __m128i *)data));
        data += 16;
        len -= 16;
    }

    // 128 bits to 64
    t  = _mm_clmulepi64_si128(x1, k3k4, 0x10);
    x1 = _mm_srli_si128(x1, 8);
    x1 = _mm_xor_si128(x1, t);

    t  = _mm_srli_si128(x1, 4);
    x1 = _mm_and_si128(x1, mask32);
    x1 = _mm_clmulepi64_si128(x1, k5, 0x00);
    x1 = _mm_xor_si128(x1, t);

    // down to 32
    t = _mm_and_si128(x1, mask32);
    t = _mm_clmulepi64_si128(t, poly_mu, 0x10);
    t = _mm_and_si128(t, mask32);
    t = _mm_clmulepi64_si128(t, poly_mu, 0x00);
    x1 = _mm_xor_si128(x1, t);

    return (uint32_t)_mm_extract_epi32(x1, 1);
}

uint32_t crc32_orderbook(const uint8_t *data, size_t len)
{
    uint32_t crc = 0xFFFFFFFF;

    if (len >= 64) {
        size_t bulk = len & ~(size_t)15;
        crc = crc32_fold_pclmul(data, bulk, crc);
        data += bulk;
        len -= bulk;
    }

    while (len--) {
        crc ^= *data++;
        for (int bit = 0; bit < 8; bit++) {
            crc = (crc >> 1) ^ (0xEDB88320 & (uint32_t)-(int32_t)(crc & 1));
        }
    }

    return ~crc;
}

#else
#error "Unsupported architecture. Please contact the author to have support added"
#endif
