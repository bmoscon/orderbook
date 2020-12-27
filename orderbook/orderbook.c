/*
Copyright (C) 2020  Bryant Moscon - bmoscon@gmail.com

Please see the LICENSE file for the terms and conditions
associated with this software.
*/
#include "orderbook.h"
#include "utils.h"


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
        self->bids = (SortedDict *)SortedDict_new(&SortedDictType, NULL, NULL);
        self->bids->ordering = -1;
        if (!self->bids) {
            Py_DECREF(self);
            return NULL;
        }
        Py_INCREF(self->bids);

        self->asks = (SortedDict *)SortedDict_new(&SortedDictType, NULL, NULL);
        self->asks->ordering = 1;
        if (!self->asks) {
            Py_DECREF(self->bids);
            Py_DECREF(self);
            return NULL;
        }

        Py_INCREF(self->asks);
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


/* Orderbook methods */
static PyObject* Orderbook_todict(Orderbook *self, PyObject *Py_UNUSED(ignored))
{
    PyObject *ret = PyDict_New();
    if (!ret) {
        return NULL;
    }

    PyObject *bids = SortedDict_todict(self->bids, NULL);
    if (!bids) {
        Py_DECREF(ret);
        return NULL;
    }

    PyObject *asks = SortedDict_todict(self->asks, NULL);
    if (!asks) {
        Py_DECREF(bids);
        Py_DECREF(ret);
        return NULL;
    }

    if (PyDict_SetItemString(ret, "bid", bids) < 0) {
        Py_DECREF(asks);
        Py_DECREF(bids);
        Py_DECREF(ret);
        return NULL;
    }

    if (PyDict_SetItemString(ret, "ask", asks) < 0) {
        Py_DECREF(asks);
        Py_DECREF(bids);
        Py_DECREF(ret);
        return NULL;
    }

    Py_DECREF(asks);
    Py_DECREF(bids);
    return ret;
}


/* Orderbook Mapping Functions */
Py_ssize_t Orderbook_len(Orderbook *self)
{
	return SortedDict_len(self->bids) + SortedDict_len(self->asks);
}


PyObject *Orderbook_getitem(Orderbook *self, PyObject *key)
{
    if (!PyUnicode_Check(key)) {
        PyErr_SetString(PyExc_ValueError, "key must one of bid/ask");
        return NULL;
    }

    PyObject *str = PyUnicode_AsEncodedString(key, "UTF-8", "strict");
    if (!str) {
        return NULL;
    }

    int key_int = check_key(PyBytes_AsString(str));

    //1 is bid, 2 is ask
    if (key_int == 1) {
        Py_INCREF(self->bids);
        Py_DECREF(str);
        return (PyObject *)self->bids;
    } else if (key_int == 2) {
        Py_INCREF(self->asks);
        Py_DECREF(str);
        return (PyObject *)self->asks;
    }

    // key not or bid or ask
    Py_DECREF(str);
    PyErr_SetString(PyExc_KeyError, "key does not exist");
    return NULL;
}


int Orderbook_setitem(Orderbook *self, PyObject *key, PyObject *value)
{
    return -1;
    /*
    if (!PyDict_Check(value)) {
        PyErr_SetString(PyExc_ValueError, "can only set to a dict");
        return -1
    }

    if (value) {
        return PyDict_SetItem(self->data, key, value);
    } else {
        // setitem also called to for del (value will be null for deletes)
        return PyDict_DelItem(self->data, key);
    }
    */
}


PyMODINIT_FUNC PyInit_order_book(void)
{
    PyObject *m;
    if (PyType_Ready(&OrderbookType) < 0 || PyType_Ready(&SortedDictType) < 0)
        return NULL;

    m = PyModule_Create(&orderbookmodule);
    if (m == NULL)
        return NULL;

    Py_INCREF(&OrderbookType);
    if (PyModule_AddObject(m, "OrderBook", (PyObject *) &OrderbookType) < 0) {
        Py_DECREF(&OrderbookType);
        Py_DECREF(m);
        return NULL;
    }

    Py_INCREF(&SortedDictType);
    if (PyModule_AddObject(m, "SortedDict", (PyObject *) &SortedDictType) < 0) {
        Py_DECREF(&SortedDictType);
        Py_DECREF(m);
        return NULL;
    }

    return m;
}