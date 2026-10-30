/*
Copyright (C) 2020-2026  Bryant Moscon - bmoscon@gmail.com

Please see the LICENSE file for the terms and conditions
associated with this software.
*/
#include "orderbook.h"
#include "utils.h"


// levels per side each format hashes
#define KRAKEN_DEPTH 10
#define ALTERNATING_DEPTH 25

// the spec for format(x, 'f'). interned, so immortal
static PyObject *format_f;

typedef int (*string_builder_t)(PyObject *pydata, uint8_t *data, int *pos, int size);

static PyObject *calculate_checksum(const Orderbook *ob, uint8_t *buffer);


static void Orderbook_dealloc(Orderbook *self)
{
    PyObject_GC_UnTrack(self);
    Py_CLEAR(self->bids);
    Py_CLEAR(self->asks);
    Py_TYPE(self)->tp_free((PyObject *) self);
}


static int Orderbook_traverse(Orderbook *self, visitproc visit, void *arg)
{
    Py_VISIT(self->bids);
    Py_VISIT(self->asks);
    return 0;
}


static int Orderbook_clear(Orderbook *self)
{
    // the two sides are emptied rather than dropped, which is enough to break any cycle
    if (self->bids) {
        SortedDict_clear(self->bids);
    }

    if (self->asks) {
        SortedDict_clear(self->asks);
    }

    return 0;
}


static PyObject *Orderbook_new(PyTypeObject *type, PyObject *args, PyObject *kwds)
{
    Orderbook *self = (Orderbook *) type->tp_alloc(type, 0);
    if (!self) {
        return NULL;
    }

    self->bids = (SortedDict *) SortedDict_new(&SortedDictType, NULL, NULL);
    self->asks = (SortedDict *) SortedDict_new(&SortedDictType, NULL, NULL);
    if (!self->bids || !self->asks) {
        Py_DECREF(self);
        return NULL;
    }

    self->bids->ordering = DESCENDING;
    return (PyObject *) self;
}


static int locked_init(Orderbook *self, PyObject *args, PyObject *kwds)
{
    static char *kwlist[] = {"max_depth", "max_depth_strict", "checksum_format", NULL};
    static const struct { const char *name; enum Checksums format; } formats[] = {
        {"KRAKEN", KRAKEN}, {"OKX", OKX}, {"OKCOIN", OKX}, {"BITGET", BITGET}, {"BITFINEX", BITFINEX}
    };
    int max_depth = self->max_depth;
    int truncate = self->truncate;
    const char *format_name = NULL;

    if (!PyArg_ParseTupleAndKeywords(args, kwds, "|ipz", kwlist, &max_depth, &truncate, &format_name)) {
        return -1;
    }

    if (EXPECT(max_depth < 0, 0)) {
        PyErr_SetString(PyExc_ValueError, "max_depth cannot be negative");
        return -1;
    }

    enum Checksums format = NO_CHECKSUM;
    if (format_name) {
        for (size_t i = 0; i < sizeof(formats) / sizeof(formats[0]); ++i) {
            if (!strcmp(format_name, formats[i].name)) {
                format = formats[i].format;
            }
        }

        if (format == NO_CHECKSUM) {
            PyErr_SetString(PyExc_TypeError, "invalid checksum format specified");
            return -1;
        }
    }

    // everything validated, so commit
    self->max_depth = max_depth;
    self->truncate = truncate;
    self->checksum = format;

    SortedDict *sides[] = {self->bids, self->asks};
    for (int i = 0; i < 2; ++i) {
        sides[i]->depth = max_depth;
        sides[i]->truncate = truncate;
        if (truncate && EXPECT(truncate_to_depth(sides[i]), 0)) {
            return -1;
        }
    }

    return 0;
}


static int Orderbook_init(Orderbook *self, PyObject *args, PyObject *kwds)
{
    int ret;
    Py_BEGIN_CRITICAL_SECTION2(self->bids, self->asks);
    ret = locked_init(self, args, kwds);
    Py_END_CRITICAL_SECTION2();
    return ret;
}


/* Orderbook methods */
static PyObject *Orderbook_todict(const Orderbook *self, PyObject *args, PyObject *kwargs)
{
    static char *kwlist[] = {"from_type", "to_type", NULL};
    PyObject *from = NULL;
    PyObject *to = NULL;

    if (!PyArg_ParseTupleAndKeywords(args, kwargs, "|$OO", kwlist, &from, &to)) {
        return NULL;
    }

    PyObject *bids;
    PyObject *asks = NULL;
    PyObject *ret = NULL;

    Py_BEGIN_CRITICAL_SECTION2(self->bids, self->asks);
    bids = locked_SortedDict_todict(self->bids, from, to);
    if (EXPECT(bids != NULL, 1)) {
        asks = locked_SortedDict_todict(self->asks, from, to);
    }
    Py_END_CRITICAL_SECTION2();

    if (EXPECT(bids && asks, 1)) {
        ret = Py_BuildValue("{sOsO}", "bid", bids, "ask", asks);
    }

    Py_XDECREF(bids);
    Py_XDECREF(asks);
    return ret;
}


static PyObject *locked_checksum(const Orderbook *self)
{
    uint8_t buffer[CHECKSUM_BUFFER_MAX];

    for (int attempt = 0; ; ++attempt) {
        if (EXPECT(update_keys(self->bids), 0) || EXPECT(update_keys(self->asks), 0)) {
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

        // a cached key is gone from its dict
        SortedDict_invalidate(self->bids);
        SortedDict_invalidate(self->asks);
        PyErr_Clear();
    }
}


static PyObject *Orderbook_checksum(const Orderbook *self, PyObject *Py_UNUSED(ignored))
{
    PyObject *ret;
    Py_BEGIN_CRITICAL_SECTION2(self->bids, self->asks);
    ret = locked_checksum(self);
    Py_END_CRITICAL_SECTION2();
    return ret;
}


/* Orderbook Mapping Functions */
static Py_ssize_t Orderbook_len(const Orderbook *self)
{
    Py_ssize_t ret;
    Py_BEGIN_CRITICAL_SECTION2(self->bids, self->asks);
    ret = locked_SortedDict_len(self->bids) + locked_SortedDict_len(self->asks);
    Py_END_CRITICAL_SECTION2();
    return ret;
}


// the side a key names. unknown names raise 'missing' so the two mapping slots can differ
static SortedDict *side_for_key(const Orderbook *self, PyObject *key, PyObject *missing)
{
    if (EXPECT(!PyUnicode_Check(key), 0)) {
        PyErr_SetString(PyExc_ValueError, "key must be one of bid/ask");
        return NULL;
    }

    Py_ssize_t len;
    const char *name = PyUnicode_AsUTF8AndSize(key, &len);
    if (EXPECT(!name, 0)) {
        return NULL;
    }

    switch (check_key(name, (size_t) len)) {
        case BID:
            return self->bids;
        case ASK:
            return self->asks;
        default:
            PyErr_SetString(missing, "key must be one of bid/ask");
            return NULL;
    }
}


static PyObject *Orderbook_getitem(const Orderbook *self, PyObject *key)
{
    SortedDict *side = side_for_key(self, key, PyExc_KeyError);
    return side ? Py_NewRef((PyObject *) side) : NULL;
}


// assigning a dict to a side replaces that side wholesale
static int Orderbook_setitem(const Orderbook *self, PyObject *key, PyObject *value)
{
    SortedDict *side = side_for_key(self, key, PyExc_ValueError);
    if (EXPECT(!side, 0)) {
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

    int ret;
    Py_BEGIN_CRITICAL_SECTION(side);
    ret = SortedDict_replace(side, copy);
    Py_END_CRITICAL_SECTION();
    return ret;
}


static int Orderbook_setattr(PyObject *self, PyObject *attr, PyObject *value)
{
    // the sides are assigned through setitem; anything else is an ordinary attribute
    if (PyUnicode_Check(attr)) {
        Py_ssize_t len;
        const char *name = PyUnicode_AsUTF8AndSize(attr, &len);
        if (EXPECT(!name, 0)) {
            return -1;
        }

        if (check_key(name, (size_t) len) != INVALID_SIDE) {
            return Orderbook_setitem((Orderbook *) self, attr, value);
        }
    }

    return PyObject_GenericSetAttr(self, attr, value);
}


// max_depth is a getter rather than a PyMemberDef entry: a member descriptor reads the
// field without a lock (a relaxed atomic load on free-threaded builds), which is a data
// race against the store locked_init makes while holding both sides. The side pointers
// stay members: they are set once in Orderbook_new and never reassigned
static PyObject *Orderbook_get_max_depth(Orderbook *self, void *Py_UNUSED(closure))
{
    int ret;
    Py_BEGIN_CRITICAL_SECTION2(self->bids, self->asks);
    ret = self->max_depth;
    Py_END_CRITICAL_SECTION2();
    return PyLong_FromLong(ret);
}


/* type definition */
static PyMemberDef Orderbook_members[] = {
    {"bids", Py_T_OBJECT_EX, offsetof(Orderbook, bids), Py_READONLY, "bids"},
    {"bid", Py_T_OBJECT_EX, offsetof(Orderbook, bids), Py_READONLY, "bids"},
    {"asks", Py_T_OBJECT_EX, offsetof(Orderbook, asks), Py_READONLY, "asks"},
    {"ask", Py_T_OBJECT_EX, offsetof(Orderbook, asks), Py_READONLY, "asks"},
    {NULL}
};

static PyGetSetDef Orderbook_getset[] = {
    {"max_depth", (getter) Orderbook_get_max_depth, NULL, "maximum book depth", NULL},
    {NULL}
};

static PyMethodDef Orderbook_methods[] = {
    {"to_dict", (PyCFunction) Orderbook_todict, METH_VARARGS | METH_KEYWORDS, "return a python dictionary with bids and asks"},
    {"checksum", (PyCFunction) Orderbook_checksum, METH_NOARGS, "calculate checksum using top N levels"},
    {NULL}
};

static PyMappingMethods Orderbook_mapping = {
    (lenfunc) Orderbook_len,
    (binaryfunc) Orderbook_getitem,
    (objobjargproc) Orderbook_setitem
};

static PyTypeObject OrderbookType = {
    PyVarObject_HEAD_INIT(NULL, 0)
    .tp_name = "order_book.OrderBook",
    .tp_doc = "An Orderbook data structure",
    .tp_basicsize = sizeof(Orderbook),
    .tp_flags = Py_TPFLAGS_DEFAULT | Py_TPFLAGS_BASETYPE | Py_TPFLAGS_HAVE_GC,
    .tp_new = Orderbook_new,
    .tp_init = (initproc) Orderbook_init,
    .tp_dealloc = (destructor) Orderbook_dealloc,
    .tp_traverse = (traverseproc) Orderbook_traverse,
    .tp_clear = (inquiry) Orderbook_clear,
    .tp_members = Orderbook_members,
    .tp_getset = Orderbook_getset,
    .tp_methods = Orderbook_methods,
    .tp_as_mapping = &Orderbook_mapping,
    .tp_setattro = Orderbook_setattr,
};

static PyModuleDef orderbookmodule = {
    PyModuleDef_HEAD_INIT,
    .m_name = "order_book",
    .m_doc = "Orderbook data structure",
    .m_size = -1,
};


PyMODINIT_FUNC PyInit_order_book(void)
{
    if (crc32_orderbook_init() != 0) {
        PyErr_SetString(PyExc_ImportError, "orderbook requires CRC32 CPU support");
        return NULL;
    }

    format_f = PyUnicode_InternFromString("f");
    if (!format_f) {
        return NULL;
    }

    PyObject *m = PyModule_Create(&orderbookmodule);
    if (!m) {
        return NULL;
    }

#ifdef Py_GIL_DISABLED
    if (PyUnstable_Module_SetGIL(m, Py_MOD_GIL_NOT_USED) < 0) {
        Py_DECREF(m);
        return NULL;
    }
#endif

    if (PyModule_AddType(m, &OrderbookType) < 0 || PyModule_AddType(m, &SortedDictType) < 0 || PyType_Ready(&SortedDictIterType) < 0) {
        Py_DECREF(m);
        return NULL;
    }

    return m;
}


/* Checksums */
static int checksum_overflow(void)
{
    PyErr_SetString(PyExc_ValueError, "book values too long for this checksum format");
    return -1;
}


static int append_byte(uint8_t *data, int *pos, int size, uint8_t byte)
{
    if (EXPECT(*pos >= size, 0)) {
        return checksum_overflow();
    }

    data[(*pos)++] = byte;
    return 0;
}


// append a str object's utf8, which is cached on the object
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


// str(x)
static int append_object_str(PyObject *pydata, uint8_t *data, int *pos, int size)
{
    PyObject *repr = PyObject_Str(pydata);
    if (EXPECT(!repr, 0)) {
        return -1;
    }

    int ret = append_str(repr, data, pos, size);
    Py_DECREF(repr);
    return ret;
}


// format(x, 'f'): positional notation for a value str() renders with an exponent
static int append_object_fixed(PyObject *pydata, uint8_t *data, int *pos, int size)
{
    PyObject *repr = PyObject_Format(pydata, format_f);
    if (EXPECT(!repr, 0)) {
        return -1;
    }

    int ret = append_str(repr, data, pos, size);
    Py_DECREF(repr);
    return ret;
}


// kraken hashes the digits only: no decimal point, no leading zeros
static int kraken_string_builder(PyObject *pydata, uint8_t *data, int *pos, int size)
{
    int start = *pos;

    if (EXPECT(append_object_str(pydata, data, pos, size), 0)) {
        return -1;
    }

    if (EXPECT(memchr(&data[start], 'E', *pos - start) != NULL, 0)) {
        // scientific notation hides digits, so render positionally
        *pos = start;
        if (EXPECT(append_object_fixed(pydata, data, pos, size), 0)) {
            return -1;
        }
    }

    int out = start;
    bool leading = true;
    for (int i = start; i < *pos; ++i) {
        uint8_t c = data[i];
        if (c == '.' || (c == '0' && leading)) {
            continue;
        }
        leading = false;
        data[out++] = c;
    }

    *pos = out;
    return 0;
}


// a format whose repairs are usually byte patches renders everything with str()
// first and scans the buffer once; true when a value needs the format's own builder
typedef bool (*repair_t)(uint8_t *data, int length);

// javascript writes a negative exponent as 1e-8, which is a byte patch. a positive one is positional
static bool bitfinex_needs_rebuild(uint8_t *data, int length)
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


// okx and bitget hash the wire string, which is positional
// repaired per value, since a small priced asset puts an exponent on every price
static int okx_string_builder(PyObject *pydata, uint8_t *data, int *pos, int size)
{
    int start = *pos;

    if (EXPECT(append_object_str(pydata, data, pos, size), 0)) {
        return -1;
    }

    if (EXPECT(memchr(&data[start], 'E', *pos - start) != NULL, 0)) {
        *pos = start;
        return append_object_fixed(pydata, data, pos, size);
    }

    return 0;
}


// bitfinex hashes javascript's rendering, 1e-8 for small values, positional otherwise
static int bitfinex_string_builder(PyObject *pydata, uint8_t *data, int *pos, int size)
{
    int start = *pos;

    if (EXPECT(append_object_str(pydata, data, pos, size), 0)) {
        return -1;
    }

    uint8_t *exponent = memchr(&data[start], 'E', *pos - start);
    if (EXPECT(exponent != NULL, 0)) {
        if (exponent + 1 < &data[*pos] && exponent[1] == '-') {
            *exponent = 'e';
        } else {
            *pos = start;
            return append_object_fixed(pydata, data, pos, size);
        }
    }

    return 0;
}


// the top of one side, a window of cached keys and the dict they index
typedef struct {
    PyObject *keys;
    PyObject *contents;
    Py_ssize_t levels;
    bool more;   // the book holds levels beyond the window
} side_snapshot;


// snapshot the top 'limit' levels, or every level when limit is negative
static int snapshot_side(SortedDict *side, Py_ssize_t limit, side_snapshot *snap)
{
    Py_ssize_t total = locked_SortedDict_len(side);
    if (total > side->k_len) {
        total = side->k_len;
    }

    Py_ssize_t levels = (limit >= 0 && total > limit) ? limit : total;

    snap->keys = SortedDict_key_window(side, levels);
    snap->contents = Py_NewRef(side->data);
    snap->levels = levels;
    snap->more = levels < total;

    return snap->keys ? 0 : -1;
}


static void release_side(side_snapshot *snap)
{
    Py_CLEAR(snap->contents);
    Py_CLEAR(snap->keys);
}


// new references to the price and value at a level
static int snapshot_level(const side_snapshot *snap, Py_ssize_t index, PyObject **price, PyObject **amount)
{
    PyObject *key = Py_NewRef(PyTuple_GET_ITEM(snap->keys, index));
    int found = PyDict_GetItemRef(snap->contents, key, amount);

    if (EXPECT(found <= 0, 0)) {
        if (found == 0) {
            PyErr_SetObject(PyExc_KeyError, key);
        }
        Py_DECREF(key);
        return -1;
    }

    *price = key;
    return 0;
}


static int kraken_populate_side(const side_snapshot *snap, uint8_t *data, int *pos)
{
    for (Py_ssize_t i = 0; i < snap->levels; ++i) {
        PyObject *price;
        PyObject *amount;

        if (EXPECT(snapshot_level(snap, i, &price, &amount), 0)) {
            return -1;
        }

        int ret = kraken_string_builder(price, data, pos, CHECKSUM_BUFFER_MAX) || kraken_string_builder(amount, data, pos, CHECKSUM_BUFFER_MAX);
        Py_DECREF(amount);
        Py_DECREF(price);

        if (EXPECT(ret, 0)) {
            return -1;
        }
    }

    return 0;
}


// the top 10 asks then the top 10 bids, price then amount, no separators
static PyObject *kraken_checksum(const Orderbook *ob, uint8_t *buffer)
{
    if (EXPECT(ob->max_depth && ob->max_depth < KRAKEN_DEPTH, 0)) {
        PyErr_SetString(PyExc_ValueError, "Max depth is less than usual number of levels for Kraken checksum");
        return NULL;
    }

    side_snapshot asks = {0};
    side_snapshot bids = {0};
    PyObject *ret = NULL;
    int pos = 0;

    if (!snapshot_side(ob->asks, KRAKEN_DEPTH, &asks) && !snapshot_side(ob->bids, KRAKEN_DEPTH, &bids)
        && !kraken_populate_side(&asks, buffer, &pos) && !kraken_populate_side(&bids, buffer, &pos)) {
        ret = PyLong_FromUnsignedLong(crc32_orderbook(buffer, pos));
    }

    release_side(&bids);
    release_side(&asks);
    return ret;
}


// walks one side entry by entry. an L3 level holds several orders, which are
// hashed one each by ascending id rather than in the order the level holds them
typedef struct {
    const side_snapshot *snap;
    PyObject *orders;    // the open level's sorted ids, or (id, amount) pairs without the GIL
    PyObject *amounts;   // the open level's dict, GIL build only
    Py_ssize_t level;
    Py_ssize_t order;
    bool expand_orders;
} side_cursor;


static void cursor_close_level(side_cursor *cursor)
{
    Py_CLEAR(cursor->orders);
    Py_CLEAR(cursor->amounts);
}


static int cursor_open_level(side_cursor *cursor, PyObject *level)
{
#ifdef Py_GIL_DISABLED
    // another thread can mutate the level dict directly, so take the pairs in one step
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


// the next (field, amount) to hash, as new references. 1 produced, 0 exhausted, -1 error
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
                int found = PyDict_GetItemRef(cursor->amounts, entry, amount);
                if (EXPECT(found <= 0, 0)) {
                    if (found == 0) {
                        PyErr_SetObject(PyExc_KeyError, entry);
                    }
                    return -1;
                }
                *field = Py_NewRef(entry);
#endif
                return 1;
            }

            cursor_close_level(cursor);
        }

        if (cursor->level >= cursor->snap->levels) {
            return 0;
        }

        PyObject *price;
        PyObject *value;
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


// field:amount: for the cursor's next entry. 1 appended, 0 exhausted, -1 error
static int append_entry(side_cursor *cursor, uint8_t *data, int *pos, string_builder_t string_builder, bool negate_amount)
{
    PyObject *field;
    PyObject *amount;
    const int size = CHECKSUM_BUFFER_MAX;

    int ret = cursor_next(cursor, &field, &amount);
    if (ret != 1) {
        return ret;
    }

    if (string_builder(field, data, pos, size) || append_byte(data, pos, size, ':')
        || (negate_amount && append_byte(data, pos, size, '-'))
        || string_builder(amount, data, pos, size) || append_byte(data, pos, size, ':')) {
        ret = -1;
    }

    Py_DECREF(amount);
    Py_DECREF(field);
    return ret;
}


// the interleaved bid:ask string over the top entries of each side, snapshotting
// 'window' levels a side. 0 built, 1 a side ran out of entries inside its window
// although the book holds more levels (an L3 level in the window was empty), -1 error
static int build_alternating(const Orderbook *ob, uint8_t *buffer, Py_ssize_t window, string_builder_t string_builder, bool signed_asks, bool expand_orders, int *length)
{
    side_snapshot bids = {0};
    side_snapshot asks = {0};
    side_cursor bid_cursor = {.snap = &bids, .expand_orders = expand_orders};
    side_cursor ask_cursor = {.snap = &asks, .expand_orders = expand_orders};
    int ret = -1;
    int pos = 0;
    int bid_n = 0;
    int ask_n = 0;

    if (snapshot_side(ob->bids, window, &bids) || snapshot_side(ob->asks, window, &asks)) {
        goto done;
    }

    for (int i = 0; i < ALTERNATING_DEPTH; ++i) {
        int b = append_entry(&bid_cursor, buffer, &pos, string_builder, false);
        int a = (b < 0) ? -1 : append_entry(&ask_cursor, buffer, &pos, string_builder, signed_asks);
        if (EXPECT(b < 0 || a < 0, 0)) {
            goto done;
        }
        bid_n += b;
        ask_n += a;
    }

    *length = (pos > 0) ? pos - 1 : 0;
    ret = ((bid_n < ALTERNATING_DEPTH && bids.more) || (ask_n < ALTERNATING_DEPTH && asks.more)) ? 1 : 0;

done:
    cursor_close_level(&ask_cursor);
    cursor_close_level(&bid_cursor);
    release_side(&asks);
    release_side(&bids);
    return ret;
}


static PyObject *alternating_checksum(const Orderbook *ob, uint8_t *buffer, repair_t needs_rebuild, string_builder_t string_builder, bool signed_asks, bool expand_orders)
{
    if (EXPECT(ob->max_depth && ob->max_depth < ALTERNATING_DEPTH, 0)) {
        PyErr_SetString(PyExc_ValueError, "Max depth is less than minimum number of levels for checksum");
        return NULL;
    }

    // snapshot only the window the string needs: an empty L3 level makes the
    // window come up short, and then the whole book is taken. a format with a
    // repair scan renders with str() first and only rebuilds if the scan says so
    Py_ssize_t window = ALTERNATING_DEPTH;
    string_builder_t builder = needs_rebuild ? append_object_str : string_builder;
    int length;

    while (true) {
        int ret = build_alternating(ob, buffer, window, builder, signed_asks, expand_orders, &length);
        if (EXPECT(ret < 0, 0)) {
            return NULL;
        }

        if (EXPECT(ret > 0, 0)) {
            window = -1;
        } else if (builder != string_builder && EXPECT(needs_rebuild(buffer, length), 0)) {
            builder = string_builder;
        } else {
            break;
        }
    }

    return PyLong_FromUnsignedLong(crc32_orderbook(buffer, length));
}


static PyObject *calculate_checksum(const Orderbook *ob, uint8_t *buffer)
{
    switch (ob->checksum) {
        case KRAKEN:
            return kraken_checksum(ob, buffer);
        case OKX:
        case BITGET:
            return alternating_checksum(ob, buffer, NULL, okx_string_builder, false, false);
        case BITFINEX:
            return alternating_checksum(ob, buffer, bitfinex_needs_rebuild, bitfinex_string_builder, true, true);
        default:
            PyErr_SetString(PyExc_ValueError, "no checksum format specified");
            return NULL;
    }
}
