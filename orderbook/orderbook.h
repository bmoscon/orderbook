/*
Copyright (C) 2020-2026  Bryant Moscon - bmoscon@gmail.com

Please see the LICENSE file for the terms and conditions
associated with this software.
*/
#ifndef __ORDERBOOK__
#define __ORDERBOOK__

#include <stdbool.h>

#define PY_SSIZE_T_CLEAN
#include "Python.h"
#include "sorteddict.h"


#define CHECKSUM_BUFFER_MAX 4096

// zero is the default. tp_alloc hands out a book without a checksum
enum Checksums {
    NO_CHECKSUM,
    KRAKEN,
    OKX,
    BITGET,
    BITFINEX
};

typedef struct {
    PyObject_HEAD
    SortedDict *bids;
    SortedDict *asks;
    int max_depth;
    enum Checksums checksum;
    bool truncate;
} Orderbook;

#endif
