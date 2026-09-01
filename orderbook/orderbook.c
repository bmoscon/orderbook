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
    SortedDict_drop_key_cache(side);
    // flush before dropping previous - finalizers can reenter
    SortedDict_flush_pending(side);

    Py_DECREF(previous);
}


void Orderbook_dealloc(Orderbook *self)
{
    PyObject_GC_UnTrack(self);
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
        self->checksum_len = 0;
    }
    return (PyObject *) self;
}


static int locked_init(Orderbook *self, PyObject *args, PyObject *kwds)
{
    static char *kwlist[] = {"max_depth", "max_depth_strict", "checksum_format", NULL};
    Py_buffer checksum_str = {0};
    int max_depth = (int) self->max_depth;

    if (!PyArg_ParseTupleAndKeywords(args, kwds, "|ipz*", kwlist, &max_depth, &self->truncate, &checksum_str)) {
        return -1;
    }

    if (EXPECT(max_depth < 0, 0)) {
        PyBuffer_Release(&checksum_str);
        PyErr_SetString(PyExc_ValueError, "max_depth cannot be negative");
        return -1;
    }

    self->max_depth = (uint32_t) max_depth;

    if (checksum_str.buf && checksum_str.len) {
        enum Checksums format;
        uint32_t buffer_len;

        if (strncmp(checksum_str.buf, "KRAKEN", checksum_str.len) == 0) {
            format = KRAKEN;
            buffer_len = 2048;
        } else if ((checksum_str.len > 2) && ((strncmp(checksum_str.buf, "OKX", 3) == 0) || (strncmp(checksum_str.buf, "OKCO", 4) == 0))) {
            format = OKX;
            buffer_len = 4096;
        } else if (strncmp(checksum_str.buf, "BITGET", checksum_str.len) == 0) {
            format = BITGET;
            buffer_len = 4096;
        } else if (strncmp(checksum_str.buf, "BITFINEX", checksum_str.len) == 0) {
            format = BITFINEX;
            buffer_len = 4096;
        } else {
            PyBuffer_Release(&checksum_str);
            PyErr_SetString(PyExc_TypeError, "invalid checksum format specified");
            return -1;
        }

        self->checksum = format;
        self->checksum_len = buffer_len;
    } else {
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


int Orderbook_init(Orderbook *self, PyObject *args, PyObject *kwds)
{
    int ret;
    Py_BEGIN_CRITICAL_SECTION2(self->bids, self->asks);
    ret = locked_init(self, args, kwds);
    Py_END_CRITICAL_SECTION2();
    return ret;
}


/* Orderbook methods */
PyObject* Orderbook_todict(const Orderbook *self, PyObject *unused, PyObject *kwargs)
{
    static char *kwlist[] = {"from_type", "to_type", NULL};
    PyObject *from = NULL;
    PyObject *to = NULL;

    if (!PyArg_ParseTupleAndKeywords(unused, kwargs, "|$OO", kwlist, &from, &to)) {
        return NULL;
    }

    PyObject *ret = PyDict_New();
    if (EXPECT(!ret, 0)) {
        return NULL;
    }

    PyObject *bids;
    PyObject *asks = NULL;

    Py_BEGIN_CRITICAL_SECTION2(self->bids, self->asks);
    bids = locked_SortedDict_todict(self->bids, from, to);
    if (EXPECT(bids != NULL, 1)) {
        asks = locked_SortedDict_todict(self->asks, from, to);
    }
    Py_END_CRITICAL_SECTION2();

    if (EXPECT(!bids || !asks, 0)) {
        Py_XDECREF(bids);
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


static PyObject *locked_checksum(const Orderbook *self)
{
    if (EXPECT(self->checksum == INVALID_CHECKSUM_FORMAT, 0)) {
        PyErr_SetString(PyExc_ValueError, "no checksum format specified");
        return NULL;
    }

    uint8_t buffer[CHECKSUM_BUFFER_MAX];

    for (int attempt = 0; ; ++attempt) {
        if (EXPECT(update_keys(self->bids), 0)) {
            return NULL;
        }

        if (EXPECT(update_keys(self->asks), 0)) {
            return NULL;
        }

        // refreshing the asks can release the lock, and a writer that got in
        // may have a change on the bids
        if (EXPECT(self->bids->dirty || self->bids->pend_count, 0) && attempt < SD_READ_RETRIES) {
            continue;
        }

        PyObject *ret = calculate_checksum(self, buffer);
        if (EXPECT(ret != NULL, 1)) {
            return ret;
        }

        if (attempt >= SD_READ_RETRIES || !PyErr_ExceptionMatches(PyExc_KeyError)) {
            return NULL;
        }

        self->bids->dirty = true;
        SortedDict_flush_pending(self->bids);
        self->asks->dirty = true;
        SortedDict_flush_pending(self->asks);

        PyErr_Clear();
    }
}


PyObject* Orderbook_checksum(const Orderbook *self, PyObject *Py_UNUSED(ignored))
{
    PyObject *ret;
    Py_BEGIN_CRITICAL_SECTION2(self->bids, self->asks);
    ret = locked_checksum(self);
    Py_END_CRITICAL_SECTION2();
    return ret;
}


/* Orderbook Mapping Functions */
Py_ssize_t Orderbook_len(const Orderbook *self)
{
    Py_ssize_t ret;
    Py_BEGIN_CRITICAL_SECTION2(self->bids, self->asks);
    ret = locked_SortedDict_len(self->bids) + locked_SortedDict_len(self->asks);
    Py_END_CRITICAL_SECTION2();
    return ret;
}


PyObject *Orderbook_getitem(const Orderbook *self, PyObject *key)
{
    if (EXPECT(!PyUnicode_Check(key), 0)) {
        PyErr_SetString(PyExc_ValueError, "key must one of bid/ask");
        return NULL;
    }

    const char *name = PyUnicode_AsUTF8(key);
    if (EXPECT(!name, 0)) {
        return NULL;
    }

    enum side_e key_int = check_key(name);

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

    const char *name = PyUnicode_AsUTF8(key);
    if (EXPECT(!name, 0)) {
        return -1;
    }

    enum side_e key_int = check_key(name);

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

    SortedDict *side = (key_int == BID) ? self->bids : self->asks;
    Py_BEGIN_CRITICAL_SECTION(side);
    replace_side(side, copy);
    Py_END_CRITICAL_SECTION();

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

    if (crc32_orderbook_init() != 0) {
        PyErr_SetString(PyExc_ImportError, "orderbook requires CRC32 CPU support");
        return NULL;
    }

    if (PyType_Ready(&OrderbookType) < 0 || PyType_Ready(&SortedDictType) < 0 || PyType_Ready(&SortedDictIterType) < 0) {
        return NULL;
    }

    m = PyModule_Create(&orderbookmodule);
    if (m == NULL)
        return NULL;

#ifdef Py_GIL_DISABLED
    if (PyUnstable_Module_SetGIL(m, Py_MOD_GIL_NOT_USED) < 0) {
        Py_DECREF(m);
        return NULL;
    }
#endif

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

    // the utf8 view is cached on the str object
    const char *string = PyUnicode_AsUTF8(repr);
    if (EXPECT(!string, 0)) {
        Py_DECREF(repr);
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
                Py_DECREF(repr);
                return checksum_overflow();
            }
            data[(*pos)++] = *string;
        }
        string++;
    }

    Py_DECREF(repr);

    return 0;
}


typedef struct {
    PyObject *keys;
    PyObject *contents;
    Py_ssize_t levels;
} side_snapshot;


static int snapshot_side(SortedDict *side, Py_ssize_t limit, side_snapshot *snap)
{
    Py_ssize_t levels = locked_SortedDict_len(side);
    Py_ssize_t cached = side->k_len;

    if (levels > cached) {
        levels = cached;
    }

    if (limit >= 0 && levels > limit) {
        levels = limit;
    }

    // copy only the window the checksum needs
    snap->keys = SortedDict_key_window(side, levels);
    if (EXPECT(!snap->keys, 0)) {
        snap->contents = NULL;
        snap->levels = 0;
        return -1;
    }

    snap->contents = Py_NewRef(side->data);
    snap->levels = levels;

    return 0;
}


static void release_side(side_snapshot *snap)
{
    Py_CLEAR(snap->contents);
    Py_CLEAR(snap->keys);
}


static int snapshot_level(const side_snapshot *snap, Py_ssize_t index, PyObject **price, PyObject **amount)
{
    PyObject *key = Py_NewRef(PyTuple_GET_ITEM(snap->keys, index));
    PyObject *value;
    int found = PyDict_GetItemRef(snap->contents, key, &value);

    if (EXPECT(found <= 0, 0)) {
        if (found == 0) {
            PyErr_SetObject(PyExc_KeyError, key);
        }
        Py_DECREF(key);
        return -1;
    }

    *price = key;
    *amount = value;

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


static PyObject* kraken_checksum(const Orderbook *ob, uint8_t *buffer)
{
    if (EXPECT(ob->max_depth && ob->max_depth < 10, 0)) {
        PyErr_SetString(PyExc_ValueError, "Max depth is less than usual number of levels for Kraken checksum");
        return NULL;
    }

    // 10 is the kraken defined number of price/size pairs to use from each side
    side_snapshot asks, bids;
    if (EXPECT(snapshot_side(ob->asks, 10, &asks), 0)) {
        return NULL;
    }

    if (EXPECT(snapshot_side(ob->bids, 10, &bids), 0)) {
        release_side(&asks);
        return NULL;
    }

    PyObject *ret = NULL;
    int pos = 0;

    if (EXPECT(kraken_populate_side(&asks, buffer, &pos, ob->checksum_len), 0)) {
        goto done;
    }

    if (EXPECT(kraken_populate_side(&bids, buffer, &pos, ob->checksum_len), 0)) {
        goto done;
    }

    ret = PyLong_FromUnsignedLong(crc32_orderbook(buffer, pos));

done:
    release_side(&bids);
    release_side(&asks);

    return ret;
}


// append the utf8 straight from the unicode object's cached utf8 view
static int append_str(PyObject *repr, uint8_t *data, int *pos, int size)
{
    Py_ssize_t len;
    const char *string = PyUnicode_AsUTF8AndSize(repr, &len);
    if (EXPECT(!string, 0)) {
        return -1;
    }

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

    int ret = append_str(repr, data, pos, size);

    Py_DECREF(repr);

    return ret;
}


static int formatf_string_builder(PyObject *pydata, uint8_t *data, int *pos, int size)
{
    OrderBookModuleState* st = get_order_book_state(NULL);

    PyObject* repr = PyObject_CallFunctionObjArgs(st->format, pydata, st->formatf, NULL);
    if (EXPECT(!repr, 0)) {
        return -1;
    }

    int ret = append_str(repr, data, pos, size);

    Py_DECREF(repr);
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


static int bitfinex_string_builder(PyObject *pydata, uint8_t *data, int *pos, int size)
{
    int startpos = *pos;
    if (EXPECT(str_string_builder(pydata, data, pos, size), 0)) {
        return -1;
    }

    uint8_t *exponent = memchr(&data[startpos], 'E', *pos - startpos);
    if (EXPECT(exponent != NULL, 0)) {
        if (exponent + 1 < &data[*pos] && exponent[1] == '-') {
            *exponent = 'e';
        } else {
            *pos = startpos;
            if (EXPECT(formatf_string_builder(pydata, data, pos, size), 0)) {
                return -1;
            }
        }
    }

    return 0;
}


typedef struct {
    const side_snapshot *snap;
    PyObject *orders;
    PyObject *amounts;
    Py_ssize_t level;
    Py_ssize_t order;
    bool expand_orders;
} side_cursor;


static void cursor_init(side_cursor *cursor, const side_snapshot *snap, bool expand_orders)
{
    cursor->snap = snap;
    cursor->orders = NULL;
    cursor->amounts = NULL;
    cursor->level = 0;
    cursor->order = 0;
    cursor->expand_orders = expand_orders;
}


static void cursor_close_level(side_cursor *cursor)
{
    Py_CLEAR(cursor->orders);
    Py_CLEAR(cursor->amounts);
}


// orders resting at one price are checksummed by ascending id, which is not the
// order the level holds them in - a dict keeps them in the order they arrived
static int cursor_open_level(side_cursor *cursor, PyObject *level)
{
#ifdef Py_GIL_DISABLED
    PyObject *orders = PyDict_Items(level);
#else
    PyObject *orders = PyDict_Keys(level);
#endif
    if (EXPECT(!orders, 0)) {
        return -1;
    }

    if (EXPECT(PyList_Sort(orders) < 0, 0)) {
        Py_DECREF(orders);
        return -1;
    }

    cursor->orders = orders;
#ifndef Py_GIL_DISABLED
    cursor->amounts = Py_NewRef(level);
#endif
    cursor->order = 0;

    return 0;
}


static int cursor_next(side_cursor *cursor, PyObject **field, PyObject **amount)
{
    while (true) {
        if (cursor->orders) {
            if (cursor->order < PyList_GET_SIZE(cursor->orders)) {
                PyObject *entry = PyList_GET_ITEM(cursor->orders, cursor->order++);
#ifdef Py_GIL_DISABLED
                *field = Py_NewRef(PyTuple_GET_ITEM(entry, 0));
                *amount = Py_NewRef(PyTuple_GET_ITEM(entry, 1));
#else
                PyObject *value;
                int found = PyDict_GetItemRef(cursor->amounts, entry, &value);

                if (EXPECT(found <= 0, 0)) {
                    if (found == 0) {
                        PyErr_SetObject(PyExc_KeyError, entry);
                    }
                    return -1;
                }

                *field = Py_NewRef(entry);
                *amount = value;
#endif
                return 1;
            }

            cursor_close_level(cursor);
        }

        if (cursor->level >= cursor->snap->levels) {
            return 0;
        }

        PyObject *price = NULL;
        PyObject *value = NULL;

        if (EXPECT(snapshot_level(cursor->snap, cursor->level++, &price, &value), 0)) {
            return -1;
        }

        if (!cursor->expand_orders || !PyDict_Check(value)) {
            *field = price;
            *amount = value;

            return 1;
        }

        int ret = cursor_open_level(cursor, value);
        Py_DECREF(value);
        Py_DECREF(price);

        if (EXPECT(ret, 0)) {
            return -1;
        }
    }
}


static int append_entry(side_cursor *cursor, uint8_t *data, int *pos, int size, char separator, string_builder_t string_builder, bool negate_amount)
{
    PyObject *field = NULL;
    PyObject *amount = NULL;

    int ret = cursor_next(cursor, &field, &amount);
    if (ret != 1) {
        return ret;
    }

    ret = -1;

    if (EXPECT(string_builder(field, data, pos, size), 0)) {
        goto done;
    }
    if (EXPECT(*pos >= size, 0)) {
        checksum_overflow();
        goto done;
    }
    data[(*pos)++] = separator;

    if (negate_amount) {
        if (EXPECT(*pos >= size, 0)) {
            checksum_overflow();
            goto done;
        }
        data[(*pos)++] = '-';
    }

    if (EXPECT(string_builder(amount, data, pos, size), 0)) {
        goto done;
    }
    if (EXPECT(*pos >= size, 0)) {
        checksum_overflow();
        goto done;
    }
    data[(*pos)++] = separator;

    ret = 1;

done:
    Py_DECREF(amount);
    Py_DECREF(field);

    return ret;
}


// build the interleaved string, and report the length to hash
static int build_alternating(const Orderbook *ob, uint8_t *buffer, const uint32_t depth, char separator, string_builder_t string_builder, bool signed_asks, bool expand_orders, int *length)
{
    side_snapshot bids, asks;
    if (EXPECT(snapshot_side(ob->bids, -1, &bids), 0)) {
        return -1;
    }
    if (EXPECT(snapshot_side(ob->asks, -1, &asks), 0)) {
        release_side(&bids);
        return -1;
    }

    side_cursor bid_cursor, ask_cursor;
    cursor_init(&bid_cursor, &bids, expand_orders);
    cursor_init(&ask_cursor, &asks, expand_orders);

    int ret = -1;
    int pos = 0;
    int buffer_len = ob->checksum_len;

    for(uint32_t i = 0; i < depth; ++i) {
        if (EXPECT(append_entry(&bid_cursor, buffer, &pos, buffer_len, separator, string_builder, false) < 0, 0)) {
            goto done;
        }

        if (EXPECT(append_entry(&ask_cursor, buffer, &pos, buffer_len, separator, string_builder, signed_asks) < 0, 0)) {
            goto done;
        }
    }

    *length = (pos > 0) ? pos - 1 : 0;
    ret = 0;

done:
    cursor_close_level(&ask_cursor);
    cursor_close_level(&bid_cursor);
    release_side(&asks);
    release_side(&bids);

    return ret;
}


static PyObject* alternating_checksum(const Orderbook *ob, uint8_t *buffer, const uint32_t depth, char separator, string_builder_t string_builder, bool signed_asks)
{
    if (EXPECT(ob->max_depth && ob->max_depth < depth, 0)) {
        PyErr_SetString(PyExc_ValueError, "Max depth is less than minimum number of levels for checksum");
        return NULL;
    }

    int length;
    if (EXPECT(build_alternating(ob, buffer, depth, separator, string_builder, signed_asks, false, &length), 0)) {
        return NULL;
    }

    return PyLong_FromUnsignedLong(crc32_orderbook(buffer, length));
}


static bool bitfinex_rerender_needed(uint8_t *data, int length)
{
    uint8_t *end = data + length;

    for (uint8_t *at = data; (at = memchr(at, 'E', end - at)); at++) {
        if (at + 1 >= end || at[1] != '-') {
            return true;
        }
        *at = 'e';
    }

    return false;
}


static PyObject* bitfinex_checksum(const Orderbook *ob, uint8_t *buffer)
{
    if (EXPECT(ob->max_depth && ob->max_depth < 25, 0)) {
        PyErr_SetString(PyExc_ValueError, "Max depth is less than minimum number of levels for checksum");
        return NULL;
    }

    int length;
    if (EXPECT(build_alternating(ob, buffer, 25, ':', str_string_builder, true, true, &length), 0)) {
        return NULL;
    }

    if (EXPECT(bitfinex_rerender_needed(buffer, length), 0)) {
        if (EXPECT(build_alternating(ob, buffer, 25, ':', bitfinex_string_builder, true, true, &length), 0)) {
            return NULL;
        }
    }

    return PyLong_FromUnsignedLong(crc32_orderbook(buffer, length));
}


static PyObject* calculate_checksum(const Orderbook *ob, uint8_t *buffer)
{
    switch (ob->checksum) {
        case KRAKEN:
            return kraken_checksum(ob, buffer);
        case OKX:
            return alternating_checksum(ob, buffer, 25, ':', okx_string_builder, false);
        case BITGET:
            return alternating_checksum(ob, buffer, 25, ':', str_string_builder, false);
        case BITFINEX:
            return bitfinex_checksum(ob, buffer);
        default:
            return NULL;
    }
}
