/*
Copyright (C) 2020  Bryant Moscon - bmoscon@gmail.com

Please see the LICENSE file for the terms and conditions
associated with this software.
*/
#ifndef __ORDERBOOK__
#define __ORDERBOOK__


#include <stdint.h>
#include <stdbool.h>

#define PY_SSIZE_T_CLEAN
#include "Python.h"
#include "structmember.h"
#include "sorteddict.h"


typedef struct {
    PyObject_HEAD
    SortedDict *bids;
    SortedDict *asks;
    uint32_t max_depth;
} Orderbook;


#endif
