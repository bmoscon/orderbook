#include <stdint.h>
#include <stdbool.h>

#define PY_SSIZE_T_CLEAN
#include "Python.h"
#include "structmember.h"


typedef struct {
    PyObject_HEAD
    PyObject *bids;
    PyObject *asks;
    uint32_t max_depth;
} Orderbook;


typedef struct {
    PyObject_HEAD
    PyObject *data;
    PyObject *keys;
    bool dirty;
    int ordering;
    int iterator_index;
    
} SortedDict;
