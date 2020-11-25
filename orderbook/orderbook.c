#define PY_SSIZE_T_CLEAN
#include <stdint.h>
#include <Python.h>

#include "structmember.h"


typedef struct {
    PyObject_HEAD
    PyObject *bids;
    PyObject *asks;
    uint32_t max_depth;
} Orderbook;


static void Orderbook_dealloc(Orderbook *self)
{
    Py_XDECREF(self->bids);
    Py_XDECREF(self->asks);
    Py_TYPE(self)->tp_free((PyObject *) self);
}


static PyObject *Orderbook_new(PyTypeObject *type, PyObject *args, PyObject *kwds)
{
    Orderbook *self;
    self = (Orderbook *) type->tp_alloc(type, 0);
    if (self != NULL) {
        self->bids = PyDict_New();
        if (!self->bids) {
            Py_DECREF(self);
            return NULL;
        }

        self->asks = PyDict_New();
        if (!self->asks) {
            Py_DECREF(self->bids);
            Py_DECREF(self);
            return NULL;
        }

        self->max_depth = 0;

    }
    return (PyObject *) self;
}


static int Orderbook_init(Orderbook *self, PyObject *args, PyObject *kwds)
{
    static char *kwlist[] = {"max_depth", NULL};

    if (!PyArg_ParseTupleAndKeywords(args, kwds, "|i", kwlist, &self->max_depth)) {
        return -1;
    }

    return 0;
}


static PyMemberDef Orderbook_members[] = {
    {"bids", T_OBJECT_EX, offsetof(Orderbook, bids), 0, "bids"},
    {"asks", T_OBJECT_EX, offsetof(Orderbook, asks), 0, "asks"},
    {"max_depth", T_INT, offsetof(Orderbook, max_depth), 0, "Maximum book depth"},
    {NULL}
};


static PyObject* Orderbook_placeholder(Orderbook *self, PyObject *Py_UNUSED(ignored))
{
    return PyUnicode_FromString("Hello!");
}


static PyMethodDef Orderbook_methods[] = {
    {"placeholder", (PyCFunction) Orderbook_placeholder, METH_NOARGS,
     "Placeholder"
    },
    {NULL}
};


static PyTypeObject OrderbookType = {
    PyVarObject_HEAD_INIT(NULL, 0)
    .tp_name = "orderbook.orderbook",
    .tp_doc = "An Orderbook data structure",
    .tp_basicsize = sizeof(Orderbook),
    .tp_itemsize = 0,
    .tp_flags = Py_TPFLAGS_DEFAULT | Py_TPFLAGS_BASETYPE,
    .tp_new = Orderbook_new,
    .tp_init = (initproc) Orderbook_init,
    .tp_dealloc = (destructor) Orderbook_dealloc,
    .tp_members = Orderbook_members,
    .tp_methods = Orderbook_methods,
};


static PyModuleDef orderbookmodule = {
    PyModuleDef_HEAD_INIT,
    .m_name = "Orderbook",
    .m_doc = "Orderbook data structure",
    .m_size = -1,
};


PyMODINIT_FUNC PyInit_orderbook(void)
{
    PyObject *m;
    if (PyType_Ready(&OrderbookType) < 0)
        return NULL;

    m = PyModule_Create(&orderbookmodule);
    if (m == NULL)
        return NULL;

    Py_INCREF(&OrderbookType);
    if (PyModule_AddObject(m, "Orderbook", (PyObject *) &OrderbookType) < 0) {
        Py_DECREF(&OrderbookType);
        Py_DECREF(m);
        return NULL;
    }

    return m;
}