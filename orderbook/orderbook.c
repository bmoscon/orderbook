/*
Copyright (C) 2020-2021  Bryant Moscon - bmoscon@gmail.com

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
        self->bids->ordering = DESCENDING;
        if (!self->bids) {
            Py_DECREF(self);
            return NULL;
        }
        Py_INCREF(self->bids);

        self->asks = (SortedDict *)SortedDict_new(&SortedDictType, NULL, NULL);
        self->asks->ordering = ASCENDING;
        if (!self->asks) {
            Py_DECREF(self->bids);
            Py_DECREF(self);
            return NULL;
        }

        Py_INCREF(self->asks);
        self->max_depth = 0;
        self->truncate = false;
    }
    return (PyObject *) self;
}


static int Orderbook_init(Orderbook *self, PyObject *args, PyObject *kwds)
{
    static char *kwlist[] = {"max_depth", "max_depth_strict", NULL};

    if (!PyArg_ParseTupleAndKeywords(args, kwds, "|ip", kwlist, &self->max_depth, &self->truncate)) {
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


static PyObject* Orderbook_checksum(Orderbook *self, PyObject *depth)
{
    if (!PyLong_CheckExact(depth)) {
        PyErr_SetString(PyExc_ValueError, "argument must be an integer");
        return NULL;
    }

    long len = PyLong_AsLong(depth);
    if (len == -1) {
        if (PyErr_Occurred()) {
            return NULL;
        }
    }

    if (len < SortedDict_len(self->asks) || len < SortedDict_len(self->bids)) {
        PyErr_SetString(PyExc_ValueError, "depth larger than book depth");
        return NULL;
    }

    if (update_keys(self->bids)) {
        return NULL;
    }

    if (update_keys(self->asks)) {
        return NULL;
    }

    uint8_t *data = calloc(1024, sizeof(uint8_t));
    int pos = 0;

    if (!data) {
        return PyErr_NoMemory();
    }

    /* asks */
    for(int i = 0; i < len; ++i) {
        PyObject *price = PyTuple_GET_ITEM(self->asks->keys, i);
        PyObject *size = PyDict_GetItem(self->asks->data, price);

        if (populate(price, data, &pos) == -1) {
            free(data);
            return NULL;
        }

        if (populate(size, data, &pos) == -1) {
            free(data);
            return NULL;
        }
    }

    /* bids */
    for(int i = 0; i < len; ++i) {
        PyObject *price = PyTuple_GET_ITEM(self->bids->keys, i);
        PyObject *size = PyDict_GetItem(self->bids->data, price);

        if (populate(price, data, &pos) == -1) {
            free(data);
            return NULL;
        }

        if (populate(size, data, &pos) == -1) {
            free(data);
            return NULL;
        }
    }

    unsigned long ret = crc32(data, pos);
    free(data);

    return PyLong_FromUnsignedLong(ret);
}


static int populate(PyObject *pydata, uint8_t *data, int *pos)
{
    PyObject *repr = PyObject_Str(pydata);
    if (!repr) {
        return -1;
    }

    PyObject* str = PyUnicode_AsEncodedString(repr, "UTF-8", "strict");
    if (!str) {
        Py_DECREF(repr);
        return -1;
    }

    const char *string = PyBytes_AS_STRING(str);
    if (!string) {
        Py_DECREF(str);
        Py_DECREF(repr);
        return -1;
    }

    const char *ptr = string;
    bool leading_zero = true;
    while (*ptr) {
        if (*ptr != '.') {
            if (*ptr != '0' && leading_zero) {
                leading_zero = false;
            }
            if (*ptr == '0' && leading_zero) {
                ptr++;
                continue;
            }
            data[(*pos)++] = *ptr;
        }
        ptr++;
    }

    return 0;
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

    enum side_e key_int = check_key(PyBytes_AsString(str));
    Py_DECREF(str);

    if (key_int == BID) {
        Py_INCREF(self->bids);
        return (PyObject *)self->bids;
    } else if (key_int == ASK) {
        Py_INCREF(self->asks);
        return (PyObject *)self->asks;
    }

    // key not bid or ask
    PyErr_SetString(PyExc_KeyError, "key does not exist");
    return NULL;
}


int Orderbook_setitem(Orderbook *self, PyObject *key, PyObject *value)
{
    if (!PyUnicode_Check(key)) {
        PyErr_SetString(PyExc_ValueError, "key must one of bid/ask");
        return -1;
    }

    PyObject *str = PyUnicode_AsEncodedString(key, "UTF-8", "strict");
    if (!str) {
        return -1;
    }

    enum side_e key_int = check_key(PyBytes_AsString(str));
    Py_DECREF(key);

    if (key_int == INVALID_SIDE) {
        PyErr_SetString(PyExc_ValueError, "key must one of bid/ask");
        Py_DECREF(str);
        return -1;
    }

    if (!value) {
        PyErr_SetString(PyExc_ValueError, "cannot delete");
        return -1;
    }

    if (!PyDict_Check(value)) {
        PyErr_SetString(PyExc_ValueError, "value must be a dict");
        return -1;
    }

    PyObject *copy = PyDict_Copy(value);
    if (!copy) {
        return -1;
    }

    if (key_int == BID) {
        Py_DECREF(self->bids->data);
        self->bids->data = copy;
        self->bids->dirty = true;
    } else if (key_int == ASK) {
        Py_DECREF(self->asks->data);
        self->asks->data = copy;
        self->asks->dirty = true;
    }

    return 0;
}


int Orderbook_setattr(PyObject *self, PyObject *attr, PyObject *value)
{
    return Orderbook_setitem((Orderbook *)self, attr, value);
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
