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
#include "structmember.h"


enum Ordering {
    ASCENDING,
    DESCENDING,
    INVALID_ORDERING
};


// pending key changes tracked. once the limit is hit, full sort of the cache
#define SD_PENDING_MAX 64

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
    // the sorted key cache: a plain array of owned refs. merges move pointers between arrays
    PyObject **karr;
    Py_ssize_t k_len;
    // consumers that need immutable snapshot get this lazily built tuple of karr, cached until the key set changes
    PyObject *keys_tuple;
    uint64_t version;
    enum Ordering ordering;
    int depth;
    uint16_t pend_count;
    bool truncate;
    // set when only a full re-sort can rebuild the cache. changes that are pended are should not toggle
    bool dirty;
    PendingEntry pend[SD_PENDING_MAX];
} SortedDict;


// side iterator
typedef struct {
    PyObject_HEAD
    PyObject *keys;
    PyObject *data;
    Py_ssize_t index;
    Py_ssize_t len;    // obeys max_depth
    bool pairs;
} SortedDictIter;

extern PyTypeObject SortedDictIterType;


void SortedDict_dealloc(SortedDict *self);
PyObject *SortedDict_new(PyTypeObject *type, PyObject *args, PyObject *kwds);
int SortedDict_init(SortedDict *self, PyObject *args, PyObject *kwds);
int SortedDict_traverse(SortedDict *self, visitproc visit, void *arg);
int SortedDict_clear(SortedDict *self);


PyObject* SortedDict_keys(SortedDict *self, PyObject *Py_UNUSED(ignored));
PyObject* SortedDict_index(SortedDict *self, PyObject *index);
PyObject* SortedDict_todict(SortedDict *self, PyObject *unused, PyObject *kwargs);
PyObject* SortedDict_todict_impl(SortedDict *self, PyObject *from, PyObject *to);
PyObject* SortedDict_tolist(SortedDict *self, PyObject *Py_UNUSED(ignored));
PyObject* SortedDict_items(SortedDict *self, PyObject *Py_UNUSED(ignored));
PyObject* SortedDict_truncate(SortedDict *self, PyObject *Py_UNUSED(ignored));

Py_ssize_t SortedDict_len(const SortedDict *self);
PyObject *SortedDict_getitem(SortedDict *self, PyObject *key);
int SortedDict_setitem(SortedDict *self, PyObject *key, PyObject *value);

int SortedDict_contains(const SortedDict *self, PyObject *value);

PyObject *SortedDict_getiter(SortedDict *self);


// SortedDict class members
static PyMemberDef SortedDict_members[] = {
    {"__data", T_OBJECT_EX, offsetof(SortedDict, data), READONLY, "internal data"},
    {"__ordering", T_INT, offsetof(SortedDict, ordering), 0, "ordering flag"},
    {"__truncate", T_BOOL, offsetof(SortedDict, truncate), 0, "truncate flag"},
    {"__max_depth", T_INT, offsetof(SortedDict, depth), 0, "maximum depth"},
    {NULL}
};

// SortedDict methods
static PyMethodDef SortedDict_methods[] = {
    {"keys", (PyCFunction) SortedDict_keys, METH_NOARGS, "return a list of keys in the sorted dictionary"},
    {"index", (PyCFunction) SortedDict_index, METH_O, "return a key, value tuple at index N"},
    {"truncate", (PyCFunction) SortedDict_truncate, METH_NOARGS, "truncate to length max_depth"},
    {"to_dict", (PyCFunction) SortedDict_todict, METH_VARARGS | METH_KEYWORDS, "return a python dictionary, sorted by keys"},
    {"to_list", (PyCFunction) SortedDict_tolist, METH_NOARGS, "return a list of key, value tuples."},
    {"items", (PyCFunction) SortedDict_items, METH_NOARGS, "return an iterator over (key, value) pairs, sorted by key"},
    {NULL}
};


// Sorted Dictionary Type Setup
static PyMappingMethods SortedDict_mapping = {
	(lenfunc)SortedDict_len,
	(binaryfunc)SortedDict_getitem,
	(objobjargproc)SortedDict_setitem
};

// Sorted Seq Setup
static PySequenceMethods SortedDict_seq = {
    .sq_contains = (objobjproc)SortedDict_contains
};

// SortedDict PyType Def
static PyTypeObject SortedDictType = {
    PyVarObject_HEAD_INIT(NULL, 0)
    .tp_name = "order_book.SortedDict",
    .tp_doc = "An SortedDict data structure",
    .tp_basicsize = sizeof(SortedDict),
    .tp_itemsize = 0,
    .tp_flags = Py_TPFLAGS_DEFAULT | Py_TPFLAGS_BASETYPE | Py_TPFLAGS_HAVE_GC,
    .tp_new = SortedDict_new,
    .tp_init = (initproc) SortedDict_init,
    .tp_dealloc = (destructor) SortedDict_dealloc,
    .tp_traverse = (traverseproc) SortedDict_traverse,
    .tp_clear = (inquiry) SortedDict_clear,
    .tp_members = SortedDict_members,
    .tp_methods = SortedDict_methods,
    .tp_as_mapping = &SortedDict_mapping,
    .tp_as_sequence = &SortedDict_seq,
    .tp_iter  = (getiterfunc) SortedDict_getiter,
    .tp_dictoffset = 0,
};

/* helpers */
int update_keys(SortedDict *self);
void SortedDict_flush_pending(SortedDict *self);
void SortedDict_drop_key_cache(SortedDict *self);
PyObject *SortedDict_key_window(SortedDict *self, Py_ssize_t want);


#endif
