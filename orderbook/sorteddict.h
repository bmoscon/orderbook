/*
Copyright (C) 2020-2022  Bryant Moscon - bmoscon@gmail.com

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

typedef struct {
    PyObject_HEAD
    PyObject *data;
    PyObject *keys;
    enum Ordering ordering;
    int iterator_index;
    int depth;
    bool truncate;
    bool dirty;
} SortedDict;


void SortedDict_dealloc(SortedDict *self);
PyObject *SortedDict_new(PyTypeObject *type, PyObject *args, PyObject *kwds);
int SortedDict_init(SortedDict *self, PyObject *args, PyObject *kwds);


PyObject* SortedDict_keys(SortedDict *self, PyObject *Py_UNUSED(ignored));
PyObject* SortedDict_index(SortedDict *self, PyObject *index);
PyObject* SortedDict_todict(SortedDict *self, PyObject *unused, PyObject *kwargs);
PyObject* SortedDict_tolist(SortedDict *self, PyObject *args, PyObject *kwargs);
PyObject* SortedDict_truncate(SortedDict *self, PyObject *Py_UNUSED(ignored));

Py_ssize_t SortedDict_len(const SortedDict *self);
PyObject *SortedDict_getitem(SortedDict *self, PyObject *key);
int SortedDict_setitem(SortedDict *self, PyObject *key, PyObject *value);

int SortedDict_contains(const SortedDict *self, PyObject *value);

PyObject *SortedDict_getiter(SortedDict *self);
PyObject *SortedDict_next(SortedDict *self);


// SortedDict class members
static PyMemberDef SortedDict_members[] = {
    {"__data", T_OBJECT_EX, offsetof(SortedDict, data), READONLY, "internal data"},
    {"__ordering", T_INT, offsetof(SortedDict, ordering), 0, "ordering flag"},
    {"__truncate", T_INT, offsetof(SortedDict, truncate), 0, "truncate flag"},
    {"__max_depth", T_INT, offsetof(SortedDict, depth), 0, "maximum depth"},
    {NULL}
};

// SortedDict methods
static PyMethodDef SortedDict_methods[] = {
    {"keys", (PyCFunction) SortedDict_keys, METH_NOARGS, "return a list of keys in the sorted dictionary"},
    {"index", (PyCFunction) SortedDict_index, METH_O, "return a key, value tuple at index N"},
    {"truncate", (PyCFunction) SortedDict_truncate, METH_NOARGS, "truncate to length max_depth"},
    {"to_dict", (PyCFunction) SortedDict_todict, METH_VARARGS | METH_KEYWORDS, "return a python dictionary, sorted by keys"},
    {"to_list", (PyCFunction) SortedDict_tolist, METH_VARARGS | METH_KEYWORDS, "return a list of key, value tuple with length specified in input argument. if no argument is passed or the argument is larger than length of self->data, return items with length of self->data."},
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
    .tp_flags = Py_TPFLAGS_DEFAULT | Py_TPFLAGS_BASETYPE,
    .tp_new = SortedDict_new,
    .tp_init = (initproc) SortedDict_init,
    .tp_dealloc = (destructor) SortedDict_dealloc,
    .tp_members = SortedDict_members,
    .tp_methods = SortedDict_methods,
    .tp_as_mapping = &SortedDict_mapping,
    .tp_as_sequence = &SortedDict_seq,
    .tp_iter  = (getiterfunc) SortedDict_getiter,
    .tp_iternext = (iternextfunc) SortedDict_next,
    .tp_dictoffset = 0,
};

/* helpers */
int update_keys(SortedDict *self);


#endif
