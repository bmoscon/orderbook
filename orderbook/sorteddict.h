/*
Copyright (C) 2020-2026  Bryant Moscon - bmoscon@gmail.com

Please see the LICENSE file for the terms and conditions
associated with this software.
*/
#ifndef __SORTEDDICT__
#define __SORTEDDICT__

#include <stdint.h>
#include <stdbool.h>

#define PY_SSIZE_T_CLEAN
#include "Python.h"


// zero is the default, tp_alloc hands out an ascending dict
enum Ordering {
    ASCENDING,
    DESCENDING
};

// pending key changes tracked. once the limit is hit, full sort of the cache
#define SD_PENDING_MAX 64
#define SD_READ_RETRIES 64

// changes to the keys are either inserts or deletes
enum PendingOp {
    PENDING_INSERT,
    PENDING_DELETE
};

typedef struct {
    PyObject *key;   // strong ref
    uint8_t op;
} PendingEntry;

typedef struct {
    PyObject_HEAD
    PyObject *data;
    // the sorted key cache - a plain array of owned refs. merges move pointers between arrays
    PyObject **karr;
    Py_ssize_t k_len;
    uint64_t version;
    enum Ordering ordering;
    int depth;
    uint16_t pend_count;
    bool truncate;
    // set when only a full sort can rebuild the cache
    bool dirty;
    PendingEntry pend[SD_PENDING_MAX];
} SortedDict;


extern PyTypeObject SortedDictType;
extern PyTypeObject SortedDictIterType;

PyObject *SortedDict_new(PyTypeObject *type, PyObject *args, PyObject *kwds);
int SortedDict_clear(SortedDict *self);

// the rest expect the caller to hold the object's critical section
int SortedDict_replace(SortedDict *self, PyObject *data);
void SortedDict_invalidate(SortedDict *self);
int update_keys(SortedDict *self);
int truncate_to_depth(SortedDict *self);
PyObject *SortedDict_key_window(SortedDict *self, Py_ssize_t want);
Py_ssize_t locked_SortedDict_len(const SortedDict *self);
PyObject *locked_SortedDict_todict(SortedDict *self, PyObject *from, PyObject *to);

#endif
