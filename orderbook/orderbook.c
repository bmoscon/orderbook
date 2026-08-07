/*
Copyright (C) 2020-2026  Bryant Moscon - bmoscon@gmail.com

Please see the LICENSE file for the terms and conditions
associated with this software.
*/
#include "orderbook.h"
#include "utils.h"


typedef int (*string_builder_t)(PyObject *pydata, uint8_t *data, int *pos, int size);


static int checksum_overflow(void)
{
    PyErr_SetString(PyExc_ValueError, "book values too long for this checksum format");
    return -1;
}

static void replace_side(SortedDict *side, PyObject *data)
{
    PyObject *previous = side->data;

    side->data = data;
    side->dirty = true;
    Py_CLEAR(side->keys);

    Py_DECREF(previous);
}


void Orderbook_dealloc(Orderbook *self)
{
    PyObject_GC_UnTrack(self);
    free(self->checksum_buffer);
    self->checksum_buffer = NULL;
    Py_CLEAR(self->bids);
    Py_CLEAR(self->asks);
    Py_TYPE(self)->tp_free((PyObject *) self);
}


int Orderbook_traverse(Orderbook *self, visitproc visit, void *arg)
{
    Py_VISIT(self->bids);
    Py_VISIT(self->asks);
    return 0;
}


int Orderbook_clear(Orderbook *self)
{
    // the two sides are emptied rather than dropped which is enough
    // to break any cycle
    if (self->bids) {
        SortedDict_clear(self->bids);
    }

    if (self->asks) {
        SortedDict_clear(self->asks);
    }

    return 0;
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
            Py_DECREF(self);
            return NULL;
        }
        self->asks->ordering = ASCENDING;

        self->max_depth = 0;
        self->truncate = false;
        self->checksum = INVALID_CHECKSUM_FORMAT;
        self->checksum_buffer = NULL;
        self->checksum_len = 0;
        self->checksumming = false;
    }
    return (PyObject *) self;
}


int Orderbook_init(Orderbook *self, PyObject *args, PyObject *kwds)
{
    static char *kwlist[] = {"max_depth", "max_depth_strict", "checksum_format", NULL};
    Py_buffer checksum_str = {0};

   // reachable because rendering a level calls __str__ (which could be re-entrant)
    if (EXPECT(self->checksumming, 0)) {
        PyErr_SetString(PyExc_RuntimeError, "cannot modify orderbook while checksumming");
        return -1;
    }

    if (!PyArg_ParseTupleAndKeywords(args, kwds, "|ipz*", kwlist, &self->max_depth, &self->truncate, &checksum_str)) {
        return -1;
    }

    if (checksum_str.buf && checksum_str.len) {
        enum Checksums format;
        uint32_t buffer_len;

        if (strncmp(checksum_str.buf, "KRAKEN", checksum_str.len) == 0) {
            format = KRAKEN;
            buffer_len = 2048;
        } else if ((checksum_str.len > 2) && (strncmp(checksum_str.buf, "FTX", 3) == 0)) {
            format = FTX;
            buffer_len = 20480;
        } else if ((checksum_str.len > 2) && ((strncmp(checksum_str.buf, "OKX", 3) == 0) || (strncmp(checksum_str.buf, "OKCO", 4) == 0))) {
            format = OKX;
            buffer_len = 4096;
        } else if (strncmp(checksum_str.buf, "BITGET", checksum_str.len) == 0) {
            format = BITGET;
            buffer_len = 4096;
        } else {
            PyBuffer_Release(&checksum_str);
            PyErr_SetString(PyExc_TypeError, "invalid checksum format specified");
            return -1;
        }

        uint8_t *buffer = calloc(buffer_len, sizeof(uint8_t));
        if (!buffer) {
            PyBuffer_Release(&checksum_str);
            PyErr_SetNone(PyExc_MemoryError);
            return -1;
        }

        // __init__ can be called more than once on the same book
        // so make sure we are properly cleaning up
        free(self->checksum_buffer);
        self->checksum = format;
        self->checksum_buffer = buffer;
        self->checksum_len = buffer_len;
    } else {
        free(self->checksum_buffer);
        self->checksum_buffer = NULL;
        self->checksum_len = 0;
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

    // see __init__
    if (EXPECT(self->checksumming, 0)) {
        PyErr_SetString(PyExc_RuntimeError, "cannot checksum while checksumming");
        return NULL;
    }

    if (EXPECT(update_keys(self->bids), 0)) {
        return NULL;
    }

    if (EXPECT(update_keys(self->asks), 0)) {
        return NULL;
    }

    memset(self->checksum_buffer, 0, self->checksum_len);

    Orderbook *book = (Orderbook *)self;
    book->checksumming = true;
    PyObject *ret = calculate_checksum(self);
    book->checksumming = false;

    return ret;
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

    const char *name = PyBytes_AsString(str);
    if (EXPECT(!name, 0)) {
        Py_DECREF(str);
        return NULL;
    }

    enum side_e key_int = check_key(name);
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

    const char *name = PyBytes_AsString(str);
    if (EXPECT(!name, 0)) {
        Py_DECREF(str);
        return -1;
    }

    enum side_e key_int = check_key(name);
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

    replace_side(key_int == BID ? self->bids : self->asks, copy);

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

    if (crc32_init() != 0) {
        PyErr_SetString(PyExc_ImportError, "orderbook requires CRC32 CPU support");
        return NULL;
    }

    if (PyType_Ready(&OrderbookType) < 0 || PyType_Ready(&SortedDictType) < 0 || PyType_Ready(&SortedDictIterType) < 0) {
        return NULL;
    }

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

    // dont use addModule here (borrowed ref), needs a strong ref
    PyObject* builtins = PyImport_ImportModule("builtins");
    if (builtins == NULL) {
        Py_DECREF(m);
        return NULL;
    }

    st->format = PyObject_GetAttrString(builtins, "format");
    Py_DECREF(builtins);
    if (st->format == NULL) {
        Py_DECREF(m);
        return NULL;
    }

    st->formatf = PyUnicode_FromString("f");
    if (st->formatf == NULL) {
        Py_CLEAR(st->format);
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


// Checksum Code
static int kraken_string_builder(PyObject *pydata, uint8_t *data, int *pos, int size)
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
            if (EXPECT(*pos >= size, 0)) {
                Py_DECREF(str);
                return checksum_overflow();
            }
            data[(*pos)++] = *string;
        }
        string++;
    }

    Py_DECREF(str);

    return 0;
}


typedef struct {
    PyObject *keys;
    PyObject *contents;
    Py_ssize_t levels;
} side_snapshot;


static void snapshot_side(const SortedDict *side, Py_ssize_t limit, side_snapshot *snap)
{
    snap->keys = Py_NewRef(side->keys);
    snap->contents = Py_NewRef(side->data);

    Py_ssize_t levels = SortedDict_len(side);
    Py_ssize_t cached = PyTuple_GET_SIZE(snap->keys);

    if (levels > cached) {
        levels = cached;
    }

    if (limit >= 0 && levels > limit) {
        levels = limit;
    }

    snap->levels = levels;
}


static void release_side(side_snapshot *snap)
{
    Py_CLEAR(snap->contents);
    Py_CLEAR(snap->keys);
}


static int snapshot_level(const side_snapshot *snap, Py_ssize_t index, PyObject **price, PyObject **amount)
{
    PyObject *key = Py_NewRef(PyTuple_GET_ITEM(snap->keys, index));
    PyObject *value = PyDict_GetItemWithError(snap->contents, key);

    if (EXPECT(!value, 0)) {
        if (!PyErr_Occurred()) {
            PyErr_SetObject(PyExc_KeyError, key);
        }
        Py_DECREF(key);
        return -1;
    }

    *price = key;
    *amount = Py_NewRef(value);

    return 0;
}


static int kraken_populate_side(const side_snapshot *snap, uint8_t *data, int *pos, int size)
{
    for(Py_ssize_t i = 0; i < snap->levels; ++i) {
        PyObject *price = NULL;
        PyObject *amount = NULL;

        if (EXPECT(snapshot_level(snap, i, &price, &amount), 0)) {
            return -1;
        }

        int ret = kraken_string_builder(price, data, pos, size);
        if (EXPECT(ret == 0, 1)) {
            ret = kraken_string_builder(amount, data, pos, size);
        }

        Py_DECREF(amount);
        Py_DECREF(price);

        if (EXPECT(ret, 0)) {
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

    // 10 is the kraken defined number of price/size pairs to use from each side
    side_snapshot asks, bids;
    snapshot_side(ob->asks, 10, &asks);
    snapshot_side(ob->bids, 10, &bids);

    PyObject *ret = NULL;
    int pos = 0;

    if (EXPECT(kraken_populate_side(&asks, ob->checksum_buffer, &pos, ob->checksum_len), 0)) {
        goto done;
    }

    if (EXPECT(kraken_populate_side(&bids, ob->checksum_buffer, &pos, ob->checksum_len), 0)) {
        goto done;
    }

    ret = PyLong_FromUnsignedLong(crc32(ob->checksum_buffer, pos));

done:
    release_side(&bids);
    release_side(&asks);

    return ret;
}


static int append_bytes(PyObject *str, uint8_t *data, int *pos, int size)
{
    const char *string = PyBytes_AS_STRING(str);
    if (EXPECT(!string, 0)) {
        return -1;
    }

    Py_ssize_t len = PyBytes_GET_SIZE(str);
    if (EXPECT(len > size - *pos, 0)) {
        return checksum_overflow();
    }

    memcpy(&data[*pos], string, len);
    *pos += len;

    return 0;
}


static int str_string_builder(PyObject *pydata, uint8_t *data, int *pos, int size)
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

    int ret = append_bytes(str, data, pos, size);

    Py_DECREF(str);

    return ret;
}


static int floatstr_string_builder(PyObject *pydata, uint8_t *data, int *pos, int size)
{
    PyObject *repr = PyObject_Str(pydata);
    if (EXPECT(!repr, 0)) {
        return -1;
    }

    PyObject *flt = PyFloat_FromString(repr);
    if (EXPECT(!flt, 0)) {
        Py_DECREF(repr);
        return -1;
    }

    Py_DECREF(repr);

    if (EXPECT(str_string_builder(flt, data, pos, size), 0)) {
        Py_DECREF(flt);
        return -1;
    }

    Py_DECREF(flt);
    return 0;
}


static int formatf_string_builder(PyObject *pydata, uint8_t *data, int *pos, int size)
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

    int ret = append_bytes(str, data, pos, size);

    Py_DECREF(str);
    return ret;
}


static int okx_string_builder(PyObject *pydata, uint8_t *data, int *pos, int size)
{
    int startpos = *pos;
    if (EXPECT(str_string_builder(pydata, data, pos, size), 0)) {
        return -1;
    }

    // default 'str' formatting is wrong when the value is in scientific notation
    if (EXPECT((long)memchr(&data[startpos], (char) 'E', *pos - startpos), (long)0)) {
        *pos = startpos;
        if (EXPECT(formatf_string_builder(pydata, data, pos, size), 0)) {
            return -1;
        }
    }

    return 0;
}


static int ftx_string_builder(PyObject *pydata, uint8_t *data, int *pos, int size)
{
    int startpos = *pos;
    if (EXPECT(str_string_builder(pydata, data, pos, size), 0)) {
        return -1;
    }

    // default 'str' formatting is wrong when the value is less than 0.0001 or in scientific notation
    int written = *pos - startpos;
    if (EXPECT((written >= 6 && !strncmp((const char *)&data[startpos], "0.0000", 6)) || memchr(&data[startpos], (char) 'E', written), 0)) {
        *pos = startpos;
        if (EXPECT(floatstr_string_builder(pydata, data, pos, size), 0)) {
            return -1;
        }
    }

    return 0;
}


// append one price/size pair followed by the separator
static int append_level(const side_snapshot *snap, Py_ssize_t index, uint8_t *data, int *pos, int size, char separator, string_builder_t string_builder)
{
    PyObject *price = NULL;
    PyObject *amount = NULL;

    if (EXPECT(snapshot_level(snap, index, &price, &amount), 0)) {
        return -1;
    }

    int ret = -1;

    if (EXPECT(string_builder(price, data, pos, size), 0)) {
        goto done;
    }
    if (EXPECT(*pos >= size, 0)) {
        checksum_overflow();
        goto done;
    }
    data[(*pos)++] = separator;

    if (EXPECT(string_builder(amount, data, pos, size), 0)) {
        goto done;
    }
    if (EXPECT(*pos >= size, 0)) {
        checksum_overflow();
        goto done;
    }
    data[(*pos)++] = separator;

    ret = 0;

done:
    Py_DECREF(amount);
    Py_DECREF(price);

    return ret;
}


static PyObject* alternating_checksum(const Orderbook *ob, const uint32_t depth, char separator, string_builder_t string_builder)
{
    if (EXPECT(ob->max_depth && ob->max_depth < depth, 0)) {
        PyErr_SetString(PyExc_ValueError, "Max depth is less than minimum number of levels for checksum");
        return NULL;
    }

    side_snapshot bids, asks;
    snapshot_side(ob->bids, -1, &bids);
    snapshot_side(ob->asks, -1, &asks);

    PyObject *ret = NULL;
    int pos = 0;
    int buffer_len = ob->checksum_len;

    for(uint32_t i = 0; i < depth; ++i) {
        if ((Py_ssize_t)i < bids.levels) {
            if (EXPECT(append_level(&bids, i, ob->checksum_buffer, &pos, buffer_len, separator, string_builder), 0)) {
                goto done;
            }
        }

        if ((Py_ssize_t)i < asks.levels) {
            if (EXPECT(append_level(&asks, i, ob->checksum_buffer, &pos, buffer_len, separator, string_builder), 0)) {
                goto done;
            }
        }
    }

    int len = (pos > 0) ? pos - 1 : 0;

    ret = PyLong_FromUnsignedLong(crc32(ob->checksum_buffer, len));

done:
    release_side(&asks);
    release_side(&bids);

    return ret;
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
