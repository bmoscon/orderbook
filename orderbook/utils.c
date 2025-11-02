/*
Copyright (C) 2020-2024  Bryant Moscon - bmoscon@gmail.com

Please see the LICENSE file for the terms and conditions
associated with this software.
*/
#include <string.h>
#include <stdint.h>
#include "utils.h"

/*
Optimized key checking using cached PyObject strings.
Uses fast pointer comparison for cached strings, avoiding conversion overhead.
*/
inline enum side_e check_key_pyobject(PyObject *key, PyObject *str_bid, PyObject *str_ask, PyObject *str_bids, PyObject *str_asks)
{
    if (!PyUnicode_Check(key)) {
        return INVALID_SIDE;
    }

    // Fast path: pointer comparison for cached strings
    if (key == str_bid || key == str_bids) {
        return BID;
    }
    if (key == str_ask || key == str_asks) {
        return ASK;
    }

    // Slow path: Unicode comparison for non cached strings
    if (PyUnicode_Compare(key, str_bid) == 0 || PyUnicode_Compare(key, str_bids) == 0) {
        return BID;
    }
    if (PyUnicode_Compare(key, str_ask) == 0 || PyUnicode_Compare(key, str_asks) == 0) {
        return ASK;
    }

    // Also check uppercase variants (though less common in Python)
    PyObject *lower = PyObject_CallMethod(key, "lower", NULL);
    if (lower) {
        enum side_e result = INVALID_SIDE;
        if (PyUnicode_Compare(lower, str_bid) == 0 || PyUnicode_Compare(lower, str_bids) == 0) {
            result = BID;
        } else if (PyUnicode_Compare(lower, str_ask) == 0 || PyUnicode_Compare(lower, str_asks) == 0) {
            result = ASK;
        }
        Py_DECREF(lower);
        return result;
    }

    // If lower() fails, clear the error and return invalid
    PyErr_Clear();
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
