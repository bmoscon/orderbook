/*
Copyright (C) 2020-2021  Bryant Moscon - bmoscon@gmail.com

Please see the LICENSE file for the terms and conditions
associated with this software.
*/
#include "orderbook.h"
#include "utils.h"

typedef int (*string_builder_t)(PyObject *pydata, uint8_t *data, int *pos);


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
        if (self->bids == NULL) {
            Py_DECREF(self);
            return NULL;
        }
        self->bids->ordering = DESCENDING;

        self->asks = (SortedDict *)SortedDict_new(&SortedDictType, NULL, NULL);
        if (self->asks == NULL) {
            Py_DECREF(self->bids);
            Py_DECREF(self);
            return NULL;
        }
        self->asks->ordering = ASCENDING;

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

    if (!PyArg_ParseTupleAndKeywords(args, kwds, "|ipz*", kwlist, &self->max_depth, &self->truncate, &checksum_str)) {
        return -1;
    }


    if (checksum_str.buf && checksum_str.len) {
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
        } else if ((checksum_str.len > 2) && ((strncmp(checksum_str.buf, "OKX", 3) == 0) || (strncmp(checksum_str.buf, "OKCO", 4) == 0))) {
            self->checksum = OKX;
            self->checksum_buffer = calloc(4096, sizeof(uint8_t));
            self->checksum_len = 4096;
            if (!self->checksum_buffer) {
                PyErr_SetNone(PyExc_MemoryError);
                return -1;
            }
        } else if (strncmp(checksum_str.buf, "BITGET", checksum_str.len) == 0) {
            self->checksum = BITGET;
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

    self->bids->depth = self->max_depth;
    self->bids->truncate = self->truncate;
    self->asks->depth = self->max_depth;
    self->asks->truncate = self->truncate;

    PyBuffer_Release(&checksum_str);

    return 0;
}


/* Orderbook methods */
PyObject* Orderbook_todict(const Orderbook *self, PyObject *unused, PyObject *kwargs)
{
    PyObject *ret = PyDict_New();
    if (EXPECT(!ret, 0)) {
        return NULL;
    }

    PyObject *bids = SortedDict_todict(self->bids, unused, kwargs);
    if (EXPECT(!bids, 0)) {
        Py_DECREF(ret);
        return NULL;
    }

    PyObject *asks = SortedDict_todict(self->asks, unused, kwargs);
    if (EXPECT(!asks, 0)) {
        Py_DECREF(bids);
        Py_DECREF(ret);
        return NULL;
    }

    if (EXPECT(PyDict_SetItemString(ret, "bid", bids) < 0, 0)) {
        Py_DECREF(asks);
        Py_DECREF(bids);
        Py_DECREF(ret);
        return NULL;
    }

    if (EXPECT(PyDict_SetItemString(ret, "ask", asks) < 0, 0)) {
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
    if (EXPECT(self->checksum == INVALID_CHECKSUM_FORMAT, 0)) {
        PyErr_SetString(PyExc_ValueError, "no checksum format specified");
        return NULL;
    }

    if (EXPECT(update_keys(self->bids), 0)) {
        return NULL;
    }

    if (EXPECT(update_keys(self->asks), 0)) {
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
    if (EXPECT(!PyUnicode_Check(key), 0)) {
        PyErr_SetString(PyExc_ValueError, "key must one of bid/ask");
        return NULL;
    }

    PyObject *str = PyUnicode_AsEncodedString(key, "UTF-8", "strict");
    if (EXPECT(!str, 0)) {
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
    if (EXPECT(!PyUnicode_Check(key), 0)) {
        PyErr_SetString(PyExc_ValueError, "key must one of bid/ask");
        return -1;
    }

    PyObject *str = PyUnicode_AsEncodedString(key, "UTF-8", "strict");
    if (EXPECT(!str, 0)) {
        return -1;
    }

    enum side_e key_int = check_key(PyBytes_AsString(str));
    Py_DECREF(str);

    if (EXPECT(key_int == INVALID_SIDE, 0)) {
        PyErr_SetString(PyExc_ValueError, "key must one of bid/ask");
        return -1;
    }

    if (EXPECT(!value, 0)) {
        PyErr_SetString(PyExc_ValueError, "cannot delete");
        return -1;
    }

    if (EXPECT(!PyDict_Check(value), 0)) {
        PyErr_SetString(PyExc_ValueError, "value must be a dict");
        return -1;
    }

    PyObject *copy = PyDict_Copy(value);
    if (EXPECT(!copy, 0)) {
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
    OrderBookModuleState *st;

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

    st = get_order_book_state(m);

    PyObject* builtins = PyImport_AddModule("builtins");
    if (builtins == NULL) {
        Py_DECREF(&SortedDictType);
        Py_DECREF(m);
        return NULL;
    }

    st->format = PyObject_GetAttrString(builtins, "format");
    Py_DECREF(builtins);
    if (st->format == NULL) {
        Py_DECREF(&SortedDictType);
        Py_DECREF(m);
        return NULL;
    }

    st->formatf = PyUnicode_FromString("f");
    if (st->formatf == NULL) {
        Py_DECREF(st->format);
        Py_DECREF(&SortedDictType);
        Py_DECREF(m);
        return NULL;
    }

    return m;
}


static int order_book_traverse(PyObject *m, visitproc visit, void *arg)
{
    OrderBookModuleState* st = get_order_book_state(m);
    Py_VISIT(st->format);
    Py_VISIT(st->formatf);
    return 0;
}


static int order_book_clear(PyObject* m)
{
    OrderBookModuleState* st = get_order_book_state(m);
    Py_CLEAR(st->format);
    Py_CLEAR(st->formatf);
    return 0;
}


static void order_book_free(PyObject *m)
{
    order_book_clear(m);
}


static OrderBookModuleState* get_order_book_state(PyObject *m)
{
    if (m == NULL) {
        return (OrderBookModuleState*) PyModule_GetState(PyState_FindModule(&orderbookmodule));
    } else {
        return (OrderBookModuleState*) PyModule_GetState(m);
    }
}


// Checksums Code
static int kraken_string_builder(PyObject *pydata, uint8_t *data, int *pos)
{
    PyObject *repr = PyObject_Str(pydata);
    if (EXPECT(!repr, 0)) {
        return -1;
    }

    PyObject* str = PyUnicode_AsEncodedString(repr, "UTF-8", "strict");
    Py_DECREF(repr);
    if (EXPECT(!str, 0)) {
        return -1;
    }

    const char *string = PyBytes_AS_STRING(str);
    if (EXPECT(!string, 0)) {
        Py_DECREF(str);
        return -1;
    }

    bool leading_zero = true;
    while (*string) {
        if (*string != '.') {
            if (*string == 'E' || *string == 'e') {
                break;
            }
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
    uint32_t size = SortedDict_len(side);
    if (size > 10) { // 10 is the kraken defined number of price/size pairs to use from each side
        size = 10;
    }

    for(uint32_t i = 0; i < size; ++i) {
        PyObject *price = PyTuple_GET_ITEM(side->keys, i);
        PyObject *size = PyDict_GetItem(side->data, price);

        if (EXPECT(kraken_string_builder(price, data, pos), 0)) {
            return -1;
        }

        if (EXPECT(kraken_string_builder(size, data, pos), 0)) {
            return -1;
        }
    }

    return 0;
}


static PyObject* kraken_checksum(const Orderbook *ob)
{
    if (EXPECT(ob->max_depth && ob->max_depth < 10, 0)) {
        PyErr_SetString(PyExc_ValueError, "Max depth is less than usual number of levels for Kraken checksum");
        return NULL;
    }

    int pos = 0;
    if (EXPECT(kraken_populate_side(ob->asks, ob->checksum_buffer, &pos), 0)) {
        return NULL;
    }

    if (EXPECT(kraken_populate_side(ob->bids, ob->checksum_buffer, &pos), 0)) {
        return NULL;
    }

    unsigned long ret = crc32_table(ob->checksum_buffer, pos);

    return PyLong_FromUnsignedLong(ret);
}




static int str_string_builder(PyObject *pydata, uint8_t *data, int *pos)
{
    PyObject *repr = PyObject_Str(pydata);
    if (EXPECT(!repr, 0)) {
        return -1;
    }

    PyObject* str = PyUnicode_AsEncodedString(repr, "UTF-8", "strict");
    Py_DECREF(repr);
    if (EXPECT(!str, 0)) {
        return -1;
    }

    const char *string = PyBytes_AS_STRING(str);
    if (EXPECT(!string, 0)) {
        Py_DECREF(str);
        return -1;
    }

    int len = strlen(string);
    memcpy(&data[*pos], string, len);
    *pos += len;

    Py_DECREF(str);

    return 0;
}


static int floatstr_string_builder(PyObject *pydata, uint8_t *data, int *pos)
{
    PyObject *repr = PyObject_Str(pydata);
    if (EXPECT(!repr, 0)) {
        return -1;
    }

    PyObject *flt = PyFloat_FromString(repr);
    if (EXPECT(str_string_builder(flt, data, pos), 0)) {
        Py_DECREF(flt);
        return -1;
    }

    Py_DECREF(flt);
    return 0;
}


static int formatf_string_builder(PyObject *pydata, uint8_t *data, int *pos)
{
    OrderBookModuleState* st = get_order_book_state(NULL);

    PyObject* repr = PyObject_CallFunctionObjArgs(st->format, pydata, st->formatf, NULL);
    if (EXPECT(!repr, 0)) {
        return -1;
    }

    PyObject* str = PyUnicode_AsEncodedString(repr, "UTF-8", "strict");
    Py_DECREF(repr);
    if (EXPECT(!str, 0)) {
        return -1;
    }

    const char *string = PyBytes_AS_STRING(str);
    if (EXPECT(!string, 0)) {
        Py_DECREF(str);
        return -1;
    }

    int len = strlen(string);
    memcpy(&data[*pos], string, len);
    *pos += len;

    Py_DECREF(str);
    return 0;
}


static int okx_string_builder(PyObject *pydata, uint8_t *data, int *pos)
{
    int startpos = *pos;
    if (EXPECT(str_string_builder(pydata, data, pos), 0)) {
        return -1;
    }

    // default 'str' formatting is wrong when the value is in scientific notation
    if (EXPECT(memchr(&data[startpos], (char) 'E', *pos - startpos), 0)) {
        *pos = startpos;
        if (EXPECT(formatf_string_builder(pydata, data, pos), 0)) {
            return -1;
        }
    }

    return 0;
}


static int ftx_string_builder(PyObject *pydata, uint8_t *data, int *pos)
{
    int startpos = *pos;
    if (EXPECT(str_string_builder(pydata, data, pos), 0)) {
        return -1;
    }

    // default 'str' formatting is wrong when the value is less than 0.0001 or in scientific notation
    if (EXPECT(!strncmp(&data[startpos], "0.0000", 6) || memchr(&data[startpos], (char) 'E', *pos - startpos), 0)) {
        *pos = startpos;
        if (EXPECT(floatstr_string_builder(pydata, data, pos), 0)) {
            return -1;
        }
    }

    return 0;
}


static PyObject* alternating_checksum(const Orderbook *ob, const uint32_t depth, char separator, string_builder_t string_builder)
{
    if (EXPECT(ob->max_depth && ob->max_depth < depth, 0)) {
        PyErr_SetString(PyExc_ValueError, "Max depth is less than minimum number of levels for checksum");
        return NULL;
    }

    int pos = 0;
    uint32_t bids_size = SortedDict_len(ob->bids);
    uint32_t asks_size = SortedDict_len(ob->asks);
    PyObject *price = NULL;
    PyObject *size = NULL;

    for(uint32_t i = 0; i < depth; ++i) { 
        if (i < bids_size) {
            price = PyTuple_GET_ITEM(ob->bids->keys, i);
            size = PyDict_GetItem(ob->bids->data, price);

            if (EXPECT(string_builder(price, ob->checksum_buffer, &pos), 0)) {
                return NULL;
            }
            ob->checksum_buffer[pos++] = separator;

            if (EXPECT(string_builder(size, ob->checksum_buffer, &pos), 0)) {
                return NULL;
            }
            ob->checksum_buffer[pos++] = separator;
        }

        if (i < asks_size) {
            price = PyTuple_GET_ITEM(ob->asks->keys, i);
            size = PyDict_GetItem(ob->asks->data, price);

            if (EXPECT(string_builder(price, ob->checksum_buffer, &pos), 0)) {
                return NULL;
            }
            ob->checksum_buffer[pos++] = separator;

            if (EXPECT(string_builder(size, ob->checksum_buffer, &pos), 0)) {
                return NULL;
            }
            ob->checksum_buffer[pos++] = separator;
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
            return alternating_checksum(ob, 100, ':', ftx_string_builder);
        case OKX:
            return alternating_checksum(ob, 25, ':', okx_string_builder);
        case BITGET:
            return alternating_checksum(ob, 25, ':', str_string_builder);
        default:
            return NULL;
    }
}
