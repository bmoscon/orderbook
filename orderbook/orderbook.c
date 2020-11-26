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

/* Sorted Dictionary */
typedef struct {
    PyObject_HEAD
    PyObject *data;
} SortedDict;


static void SortedDict_dealloc(SortedDict *self)
{
    Py_XDECREF(self->data);
    Py_TYPE(self)->tp_free((PyObject *) self);
}


static PyObject *SortedDict_new(PyTypeObject *type, PyObject *args, PyObject *kwds)
{
    SortedDict *self;
    self = (SortedDict *) type->tp_alloc(type, 0);
    if (self != NULL) {
        self->data = PyDict_New();
        if (!self->data) {
            Py_DECREF(self);
            return NULL;
        }
    }
    return (PyObject *) self;
}


static int SortedDict_init(SortedDict *self, PyObject *args, PyObject *kwds)
{
    return 0;
}


static PyMemberDef SortedDict_members[] = {
    {"__data", T_OBJECT_EX, offsetof(SortedDict, data), READONLY, "internal data"},
    {NULL}
};


static PyObject* SortedDict_keys(SortedDict *self, PyObject *Py_UNUSED(ignored))
{
    PyObject *keys = PyDict_Keys(self->data);
    if (PyList_Sort(keys) < 0) {
        return NULL;
    }
    return keys;
}


static PyMethodDef SortedDict_methods[] = {
    {"keys", (PyCFunction) SortedDict_keys, METH_NOARGS, "return a list of keys in the sorted dictionary"
    },
    {NULL}
};

/* Sorted Dictionary Mapping Functions */
Py_ssize_t SortedDict_len(SortedDict *self) {
	return PyDict_Size(self->data);
}

PyObject *SortedDict_getitem(SortedDict *self, PyObject *key) {
    PyObject *ret = PyDict_GetItemWithError(self->data, key);
    Py_INCREF(ret);

    return ret;
}


int SortedDict_setitem(SortedDict *self, PyObject *key, PyObject *value) {
    return PyDict_SetItem(self->data, key, value);
}


/* Sorted Dictionary Type Setup */
static PyMappingMethods SortedDict_mapping = {
	(lenfunc)SortedDict_len,
	(binaryfunc)SortedDict_getitem,
	(objobjargproc)SortedDict_setitem
};


static PyTypeObject SortedDictType = {
    PyVarObject_HEAD_INIT(NULL, 0)
    .tp_name = "sorteddict.sorteddict",
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
};


/* Module specific definitions and initilization */
static PyModuleDef orderbookmodule = {
    PyModuleDef_HEAD_INIT,
    .m_name = "Orderbook",
    .m_doc = "Orderbook data structure",
    .m_size = -1,
};


PyMODINIT_FUNC PyInit_orderbook(void)
{
    PyObject *m;
    if (PyType_Ready(&OrderbookType) < 0 || PyType_Ready(&SortedDictType) < 0)
        return NULL;

    m = PyModule_Create(&orderbookmodule);
    if (m == NULL)
        return NULL;

    Py_INCREF(&OrderbookType);
    if (PyModule_AddObject(m, "orderbook", (PyObject *) &OrderbookType) < 0) {
        Py_DECREF(&OrderbookType);
        Py_DECREF(m);
        return NULL;
    }

    Py_INCREF(&SortedDictType);
    if (PyModule_AddObject(m, "sorteddict", (PyObject *) &SortedDictType) < 0) {
        Py_DECREF(&SortedDictType);
        Py_DECREF(m);
        return NULL;
    }

    return m;
}