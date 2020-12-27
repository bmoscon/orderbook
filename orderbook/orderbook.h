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


static void Orderbook_dealloc(Orderbook *self);
static PyObject *Orderbook_new(PyTypeObject *type, PyObject *args, PyObject *kwds);
static int Orderbook_init(Orderbook *self, PyObject *args, PyObject *kwds);

static PyObject* Orderbook_todict(Orderbook *self, PyObject *Py_UNUSED(ignored));

Py_ssize_t Orderbook_len(Orderbook *self);
PyObject *Orderbook_getitem(Orderbook *self, PyObject *key);
int Orderbook_setitem(Orderbook *self, PyObject *key, PyObject *value);

int Orderbook_setattr(PyObject *self, PyObject *attr, PyObject *value);


// Orderbook class members
static PyMemberDef Orderbook_members[] = {
    {"bids", T_OBJECT_EX, offsetof(Orderbook, bids), 0, "bids"},
    {"bid", T_OBJECT_EX, offsetof(Orderbook, bids), 0, "bids"},
    {"asks", T_OBJECT_EX, offsetof(Orderbook, asks), 0, "asks"},
    {"ask", T_OBJECT_EX, offsetof(Orderbook, asks), 0, "asks"},
    {"max_depth", T_INT, offsetof(Orderbook, max_depth), READONLY, "Maximum book depth"},
    {NULL}
};


// Orderbook class methods
static PyMethodDef Orderbook_methods[] = {
    {"to_dict", (PyCFunction) Orderbook_todict, METH_NOARGS, "Return a python dictionary with bids and asks"},
    {NULL}
};


// Orderbook mapping functions
static PyMappingMethods Orderbook_mapping = {
	(lenfunc)Orderbook_len,
	(binaryfunc)Orderbook_getitem,
    (objobjargproc)Orderbook_setitem
};


// Orderbook Type Setup
static PyTypeObject OrderbookType = {
    PyVarObject_HEAD_INIT(NULL, 0)
    .tp_name = "order_book.OrderBook",
    .tp_doc = "An Orderbook data structure",
    .tp_basicsize = sizeof(Orderbook),
    .tp_itemsize = 0,
    .tp_flags = Py_TPFLAGS_DEFAULT | Py_TPFLAGS_BASETYPE,
    .tp_new = Orderbook_new,
    .tp_init = (initproc) Orderbook_init,
    .tp_dealloc = (destructor) Orderbook_dealloc,
    .tp_members = Orderbook_members,
    .tp_methods = Orderbook_methods,
    .tp_as_mapping = &Orderbook_mapping,
    .tp_setattro = (setattrofunc) Orderbook_setattr,
    .tp_dictoffset = 0,
};


// Module specific definitions and initilization
static PyModuleDef orderbookmodule = {
    PyModuleDef_HEAD_INIT,
    .m_name = "order_book",
    .m_doc = "Orderbook data structure",
    .m_size = -1,
};


#endif
