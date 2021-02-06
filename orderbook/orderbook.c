/*
Copyright (C) 2020-2021  Bryant Moscon - bmoscon@gmail.com

Please see the LICENSE file for the terms and conditions
associated with this software.
*/
#include "orderbook.h"
#include "utils.h"


void Orderbook_dealloc(Orderbook *self)
{
    if (self->checksum_buffer) {
        free(self->checksum_buffer);
    }
    Py_XDECREF(self->bids);
    Py_XDECREF(self->asks);
    Py_TYPE(self)->tp_free((PyObject *) self);
}


PyObject *Orderbook_new(PyTypeObject *type, PyObject *args, PyObject *kwds)
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
        self->checksum = INVALID_CHECKSUM_FORMAT;
        self->checksum_buffer = NULL;
        self->checksum_len = 0;
    }
    return (PyObject *) self;
}


int Orderbook_init(Orderbook *self, PyObject *args, PyObject *kwds)
{
    static char *kwlist[] = {"max_depth", "max_depth_strict", "checksum_format", NULL};
    Py_buffer checksum_str = {0};

    if (!PyArg_ParseTupleAndKeywords(args, kwds, "|ips*", kwlist, &self->max_depth, &self->truncate, &checksum_str)) {
        return -1;
    }

    if (checksum_str.len) {
        if (strncmp(checksum_str.buf, "KRAKEN", checksum_str.len) == 0) {
            self->checksum = KRAKEN;
            self->checksum_buffer = calloc(2048, sizeof(uint8_t));
            self->checksum_len = 2048;
            if (!self->checksum_buffer) {
                PyErr_SetNone(PyExc_MemoryError);
                return -1;
            }
        } else if ((checksum_str.len > 2) && (strncmp(checksum_str.buf, "FTX", 3) == 0)) {
            self->checksum = FTX;
            self->checksum_buffer = calloc(20480, sizeof(uint8_t));
            self->checksum_len = 20480;
            if (!self->checksum_buffer) {
                PyErr_SetNone(PyExc_MemoryError);
                return -1;
            }
        } else if ((checksum_str.len > 3) && ((strncmp(checksum_str.buf, "OKEX", 4) == 0) || (strncmp(checksum_str.buf, "OKCO", 4) == 0))) {
            self->checksum = OKEX;
            self->checksum_buffer = calloc(4096, sizeof(uint8_t));
            self->checksum_len = 4096;
            if (!self->checksum_buffer) {
                PyErr_SetNone(PyExc_MemoryError);
                return -1;
            }
        } else {
            PyBuffer_Release(&checksum_str);
            PyErr_SetString(PyExc_TypeError, "invalid checksum format specified");
            return -1;
        }
    } else {
        self->checksum = INVALID_CHECKSUM_FORMAT;
    }

    PyBuffer_Release(&checksum_str);

    return 0;
}


/* Orderbook methods */
PyObject* Orderbook_todict(const Orderbook *self, PyObject *Py_UNUSED(ignored))
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


PyObject* Orderbook_checksum(const Orderbook *self, PyObject *Py_UNUSED(ignored))
{
    if (self->checksum == INVALID_CHECKSUM_FORMAT) {
        PyErr_SetString(PyExc_ValueError, "no checksum format specified");
        return NULL;
    }

    if (update_keys(self->bids)) {
        return NULL;
    }

    if (update_keys(self->asks)) {
        return NULL;
    }

    memset(self->checksum_buffer, 0, self->checksum_len);

    return calculate_checksum(self);
}


/* Orderbook Mapping Functions */
Py_ssize_t Orderbook_len(const Orderbook *self)
{
	return SortedDict_len(self->bids) + SortedDict_len(self->asks);
}


PyObject *Orderbook_getitem(const Orderbook *self, PyObject *key)
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


int Orderbook_setitem(const Orderbook *self, PyObject *key, PyObject *value)
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


int Orderbook_setattr(const PyObject *self, PyObject *attr, PyObject *value)
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


// Checksums Code
static int kraken_string_builder(PyObject *pydata, uint8_t *data, int *pos)
{
    PyObject *repr = PyObject_Str(pydata);
    if (!repr) {
        return -1;
    }

    PyObject* str = PyUnicode_AsEncodedString(repr, "UTF-8", "strict");
    Py_DECREF(repr);
    if (!str) {
        return -1;
    }

    const char *string = PyBytes_AS_STRING(str);
    if (!string) {
        Py_DECREF(str);
        return -1;
    }

    bool leading_zero = true;
    while (*string) {
        if (*string != '.') {
            if (*string != '0' && leading_zero) {
                leading_zero = false;
            }
            if (*string == '0' && leading_zero) {
                string++;
                continue;
            }
            data[(*pos)++] = *string;
        }
        string++;
    }

    Py_DECREF(str);

    return 0;
}


static int kraken_populate_side(const SortedDict *side, uint8_t *data, int *pos)
{
    for(int i = 0; i < 10; ++i) { // 10 is the kraken defined number of price/size pairs to use from each side
        PyObject *price = PyTuple_GET_ITEM(side->keys, i);
        PyObject *size = PyDict_GetItem(side->data, price);

        if (kraken_string_builder(price, data, pos)) {
            return -1;
        }

        if (kraken_string_builder(size, data, pos)) {
            return -1;
        }
    }

    return 0;
}


static PyObject* kraken_checksum(const Orderbook *ob)
{
    if (ob->max_depth && ob->max_depth < 10) {
        PyErr_SetString(PyExc_ValueError, "Max depth is less than minimum number of levels for Kraken checksum");
        return NULL;
    }

    int pos = 0;

    if (kraken_populate_side(ob->asks, ob->checksum_buffer, &pos)) {
        return NULL;
    }

    if (kraken_populate_side(ob->bids, ob->checksum_buffer, &pos)) {
        return NULL;
    }

    unsigned long ret = crc32_table(ob->checksum_buffer, pos);
    return PyLong_FromUnsignedLong(ret);
}


static int ftx_string_builder(PyObject *pydata, uint8_t *data, int *pos)
{
    PyObject *repr = PyObject_Str(pydata);
    if (!repr) {
        return -1;
    }

    PyObject* str = PyUnicode_AsEncodedString(repr, "UTF-8", "strict");
    Py_DECREF(repr);
    if (!str) {
        return -1;
    }

    const char *string = PyBytes_AS_STRING(str);
    if (!string) {
        Py_DECREF(str);
        return -1;
    }

    int len = strlen(string);
    memcpy(&data[*pos], string, len);
    *pos += len;
    data[(*pos)++] = ':';

    Py_DECREF(str);

    return 0;
}

static PyObject* ftx_checksum(const Orderbook *ob, const uint32_t depth)
{
    if (ob->max_depth && ob->max_depth < depth) {
        PyErr_SetString(PyExc_ValueError, "Max depth is less than minimum number of levels for checksum");
        return NULL;
    }

    int pos = 0;
    uint32_t bids_size = SortedDict_len(ob->bids);
    uint32_t asks_size = SortedDict_len(ob->asks);
    PyObject *price = NULL;
    PyObject *size = NULL;

    for(uint32_t i = 0; i < depth; ++i) { // 100 is the FTX defined number of price/size pairs to use from each side, 25 is OKEX/OKCOIN
        if (i < bids_size) {
            price = PyTuple_GET_ITEM(ob->bids->keys, i);
            size = PyDict_GetItem(ob->bids->data, price);

            if (ftx_string_builder(price, ob->checksum_buffer, &pos)) {
                return NULL;
            }

            if (ftx_string_builder(size, ob->checksum_buffer, &pos)) {
                return NULL;
            }
        }

        if (i < asks_size) {
            price = PyTuple_GET_ITEM(ob->asks->keys, i);
            size = PyDict_GetItem(ob->asks->data, price);

            if (ftx_string_builder(price, ob->checksum_buffer, &pos)) {
                return NULL;
            }

            if (ftx_string_builder(size, ob->checksum_buffer, &pos)) {
                return NULL;
            }
        }
    }

    unsigned long ret = crc32_table(ob->checksum_buffer, pos-1);

    return PyLong_FromUnsignedLong(ret);
}


static PyObject* calculate_checksum(const Orderbook *ob)
{
    switch (ob->checksum) {
        case KRAKEN:
            return kraken_checksum(ob);
        case FTX:
            return ftx_checksum(ob, 100);
        case OKEX:
            return ftx_checksum(ob, 25);
        default:
            return NULL;
    }
}
