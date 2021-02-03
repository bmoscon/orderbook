/*
Copyright (C) 2020-2021  Bryant Moscon - bmoscon@gmail.com

Please see the LICENSE file for the terms and conditions
associated with this software.
*/
#include "checksums.h"


PyObject* calculate_checksum(Orderbook *ob)
{
    switch (ob->checksum) {
        case KRAKEN:
            return kraken_checksum(ob);
            break;
        default:
            return NULL;
    }
}


static int kraken_populate(PyObject *pydata, uint8_t *data, int *pos)
{
    PyObject *repr = PyObject_Str(pydata);
    if (!repr) {
        return -1;
    }

    PyObject* str = PyUnicode_AsEncodedString(repr, "UTF-8", "strict");
    if (!str) {
        Py_DECREF(repr);
        return -1;
    }

    const char *string = PyBytes_AS_STRING(str);
    if (!string) {
        Py_DECREF(str);
        Py_DECREF(repr);
        return -1;
    }

    const char *ptr = string;
    bool leading_zero = true;
    while (*ptr) {
        if (*ptr != '.') {
            if (*ptr != '0' && leading_zero) {
                leading_zero = false;
            }
            if (*ptr == '0' && leading_zero) {
                ptr++;
                continue;
            }
            data[(*pos)++] = *ptr;
        }
        ptr++;
    }

    return 0;
}


PyObject* kraken_checksum(Orderbook *ob)
{
    if (ob->max_depth && ob->max_depth < 10) {
        PyErr_SetString(PyExc_ValueError, "Max depth is less than minimum number of levels for Kraken checksum");
        return NULL;
    }
    
    uint8_t *data = calloc(1024, sizeof(uint8_t));
    int pos = 0;

    if (!data) {
        return PyErr_NoMemory();
    }

    /* asks */
    for(int i = 0; i < 10; ++i) { // 10 is the kraken defined number of price/size pairs to use from each side
        PyObject *price = PyTuple_GET_ITEM(ob->asks->keys, i);
        PyObject *size = PyDict_GetItem(ob->asks->data, price);

        if (kraken_populate(price, data, &pos) == -1) {
            free(data);
            return NULL;
        }

        if (kraken_populate(size, data, &pos) == -1) {
            free(data);
            return NULL;
        }
    }

    /* bids */
    for(int i = 0; i < 10; ++i) { // 10 is the kraken defined number of price/size pairs to use from each side
        PyObject *price = PyTuple_GET_ITEM(ob->bids->keys, i);
        PyObject *size = PyDict_GetItem(ob->bids->data, price);

        if (kraken_populate(price, data, &pos) == -1) {
            free(data);
            return NULL;
        }

        if (kraken_populate(size, data, &pos) == -1) {
            free(data);
            return NULL;
        }
    }

    unsigned long ret = crc32(data, pos);
    free(data);

    return PyLong_FromUnsignedLong(ret);
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