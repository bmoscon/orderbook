/*
Copyright (C) 2020-2026  Bryant Moscon - bmoscon@gmail.com

Please see the LICENSE file for the terms and conditions
associated with this software.
*/
#include "sorteddict.h"
#include "utils.h"


static int truncate_to_depth(SortedDict *self);
static PyObject *SortedDict_iter_new(SortedDict *self, bool pairs);


/* Sorted Dictionary */
void SortedDict_flush_pending(SortedDict *self)
{
    for (uint16_t i = 0; i < self->pend_count; ++i) {
        Py_CLEAR(self->pend[i].key);
    }
    self->pend_count = 0;
    self->version++;
}


static void escalate_to_dirty(SortedDict *self)
{
    SortedDict_flush_pending(self);
    self->dirty = true;
}

// append to the changes log, if full set dirty bit to force a sort on next read
static void log_append(SortedDict *self, uint8_t op, PyObject *key)
{
    if (self->pend_count == SD_PENDING_MAX) {
        escalate_to_dirty(self);
        return;
    }

    self->pend[self->pend_count].key = Py_NewRef(key);
    self->pend[self->pend_count].op = op;
    self->pend_count++;
    self->version++;
}


void SortedDict_dealloc(SortedDict *self)
{
    PyObject_GC_UnTrack(self);
    for (uint16_t i = 0; i < self->pend_count; ++i) {
        Py_CLEAR(self->pend[i].key);
    }
    self->pend_count = 0;
    Py_CLEAR(self->keys);
    Py_CLEAR(self->data);
    Py_TYPE(self)->tp_free((PyObject *) self);
}


int SortedDict_traverse(SortedDict *self, visitproc visit, void *arg)
{
    Py_VISIT(self->data);
    Py_VISIT(self->keys);
    for (uint16_t i = 0; i < self->pend_count; ++i) {
        Py_VISIT(self->pend[i].key);
    }
    return 0;
}


int SortedDict_clear(SortedDict *self)
{
    SortedDict_flush_pending(self);
    Py_CLEAR(self->keys);

    // the two sides are emptied rather than dropped which is enough to break any cycle
    if (self->data) {
        PyDict_Clear(self->data);
    }
    self->dirty = true;

    return 0;
}


PyObject *SortedDict_new(PyTypeObject *type, PyObject *args, PyObject *kwds)
{
    SortedDict *self;
    self = (SortedDict *) type->tp_alloc(type, 0);
    if (self != NULL) {
        self->data = PyDict_New();
        if (!self->data) {
            Py_DECREF(self);
            return NULL;
        }

        self->ordering = INVALID_ORDERING;
        self->keys = NULL;
        self->dirty = false;
        self->depth = 0;
        self->truncate = false;
        self->pend_count = 0;
        self->version = 0;
    }

    return (PyObject *) self;
}


int SortedDict_init(SortedDict *self, PyObject *args, PyObject *kwds)
{
    PyObject *dict = NULL;

    if (PyTuple_Size(args) > 1) {
        PyErr_SetString(PyExc_TypeError, "function takes at most 1 argument");
        return -1;
    }

    if (PyTuple_Size(args) == 1) {
        dict = PyTuple_GetItem(args, 0);
        if (!dict) {
            return -1;
        }

        if (!PyDict_Check(dict)) {
            PyErr_SetString(PyExc_TypeError, "function accepts only dictionaries as an argument");
            return -1;
        }

        PyObject *copy = PyDict_Copy(dict);
        if (EXPECT(!copy, 0)) {
            return -1;
        }

        Py_XSETREF(self->data, copy);
        // the cached keys and pending log describe the old data
        Py_CLEAR(self->keys);
        escalate_to_dirty(self);
    }


    if (kwds && PyDict_Check(kwds) && PyDict_Size(kwds) > 0) {
        // borrowed refs, getItemString returns NULL when not found
        PyObject *max_depth = PyDict_GetItemString(kwds, "max_depth");
        PyObject *truncate = PyDict_GetItemString(kwds, "truncate");
        PyObject *ordering_arg = PyDict_GetItemString(kwds, "ordering");

        if (max_depth) {
            if (PyLong_Check(max_depth)) {
                self->depth = PyLong_AsLong(max_depth);
                if (self->depth == -1 && PyErr_Occurred()) {
                    return -1;
                }

                if (self->depth < 1) {
                    PyErr_SetString(PyExc_ValueError, "max_depth must be greater than 0");
                    return -1;
                }

            } else {
                PyErr_SetString(PyExc_ValueError, "max_depth must be an integer");
                return -1;
            }
        }

        if (truncate) {
            if (PyBool_Check(truncate)) {
                if (PyObject_IsTrue(truncate)) {
                    self->truncate = true;
                } else {
                    self->truncate = false;
                }
            } else {
                PyErr_SetString(PyExc_ValueError, "truncate must be a boolean");
                return -1;
            }
        }

        if (ordering_arg) {
            if (!PyUnicode_Check(ordering_arg)) {
                PyErr_SetString(PyExc_ValueError, "ordering must be a string");
                return -1;
            }

            const char *value = PyUnicode_AsUTF8(ordering_arg);
            if (!value) {
                return -1;
            }

            if (strcmp(value, "DESC") == 0) {
                self->ordering = DESCENDING;
            } else if (strcmp(value, "ASC") == 0) {
                self->ordering = ASCENDING;
            } else {
                PyErr_SetString(PyExc_ValueError, "ordering must be one of ASC or DESC");
                return -1;
            }
        } else {
            // default is ascending
            self->ordering = ASCENDING;
        }
    }

    if (self->truncate && self->data) {
        if (EXPECT(truncate_to_depth(self), 0)) {
            return -1;
        }
    }

    return 0;
}


static Py_ssize_t keys_bisect(PyObject *keys, PyObject *key, enum Ordering ordering)
{
    Py_ssize_t lo = 0;
    Py_ssize_t hi = PyTuple_GET_SIZE(keys);
    int op = (ordering == DESCENDING) ? Py_GT : Py_LT;

    while (lo < hi) {
        Py_ssize_t mid = lo + ((hi - lo) >> 1);
        int before = PyObject_RichCompareBool(PyTuple_GET_ITEM(keys, mid), key, op);

        if (EXPECT(before < 0, 0)) {
            return -1;
        }

        if (before) {
            lo = mid + 1;
        } else {
            hi = mid;
        }
    }

    return lo;
}


static int full_sort(SortedDict *self)
{
    for (int attempt = 0; ; ++attempt) {
        uint64_t version = self->version;

        PyObject *keys = PyDict_Keys(self->data);
        if (EXPECT(!keys, 0)) {
            return 1;
        }

        if (EXPECT(PyList_Sort(keys) < 0, 0)) {
            Py_DECREF(keys);
            return 1;
        }

        if (self->ordering == DESCENDING) {
            if (EXPECT(PyList_Reverse(keys) < 0, 0)) {
                Py_DECREF(keys);
                return 1;
            }
        }

        PyObject *ret = PySequence_Tuple(keys);
        Py_DECREF(keys);
        if (EXPECT(!ret, 0)) {
            return 1;
        }

        if (EXPECT(self->version == version, 1)) {
            Py_XSETREF(self->keys, ret);
            self->dirty = false;
            return 0;
        }

        if (attempt == 3) {
            Py_XSETREF(self->keys, ret);
            return 0;
        }

        Py_DECREF(ret);
    }
}


// ret 0  - key cache is up to date
// ret 1  - full sort needed
// ret -1 - exception
static int apply_pending(SortedDict *self)
{
    uint64_t version = self->version;
    PendingEntry pend[SD_PENDING_MAX];
    Py_ssize_t count = self->pend_count;

    memcpy(pend, self->pend, count * sizeof(PendingEntry));
    self->pend_count = 0;

    // the book may be mutated by any comparison or hash below so hold the refs
    PyObject *old = Py_NewRef(self->keys);
    PyObject *data = Py_NewRef(self->data);
    PyObject *inserts = NULL;
    PyObject *deletes = NULL;
    PyObject *result = NULL;
    Py_ssize_t ins_pos[SD_PENDING_MAX];
    Py_ssize_t del_pos[SD_PENDING_MAX];
    // only place status can be set to -1, so any exception/error will trigger this ret value
    int status = -1;

    inserts = PyList_New(0);
    deletes = PyList_New(0);
    if (EXPECT(!inserts || !deletes, 0)) {
        goto done;
    }

    if (count == 1) {
        if (EXPECT(PyList_Append(pend[0].op == PENDING_INSERT ? inserts : deletes, pend[0].key) < 0, 0)) {
            goto done;
        }
    } else {
        PyObject *seen = PyDict_New();
        if (EXPECT(!seen, 0)) {
            goto done;
        }

        int failed = 0;
        for (Py_ssize_t i = 0; i < count && !failed; ++i) {
            PyObject *packed = PyDict_GetItemWithError(seen, pend[i].key);
            long state;

            if (packed) {
                state = PyLong_AsLong(packed) ^ 1;
            } else if (EXPECT(PyErr_Occurred() != NULL, 0)) {
                failed = 1;
                break;
            } else {
                state = ((long)pend[i].op << 1) | 1;
            }

            packed = PyLong_FromLong(state);
            if (EXPECT(!packed, 0)) {
                failed = 1;
                break;
            }

            failed = PyDict_SetItem(seen, pend[i].key, packed) < 0;
            Py_DECREF(packed);
        }

        if (!failed) {
            PyObject *key;
            PyObject *packed;
            Py_ssize_t pos = 0;

            while (PyDict_Next(seen, &pos, &key, &packed)) {
                long state = PyLong_AsLong(packed);

                if ((state & 1) == 0) {
                    continue;
                }

                if (EXPECT(PyList_Append((state >> 1) == PENDING_INSERT ? inserts : deletes, key) < 0, 0)) {
                    failed = 1;
                    break;
                }
            }
        }

        Py_DECREF(seen);
        if (EXPECT(failed, 0)) {
            goto done;
        }
    }

    Py_ssize_t num_ins = PyList_GET_SIZE(inserts);
    Py_ssize_t num_del = PyList_GET_SIZE(deletes);

    if (num_ins == 0 && num_del == 0) {
        if (EXPECT(self->version == version && self->pend_count == 0 && !self->dirty, 1)) {
            status = 0;
        } else {
            status = 1;
        }
        goto done;
    }

    if (EXPECT(PyList_Sort(inserts) < 0, 0)) {
        goto done;
    }
    if (self->ordering == DESCENDING) {
        if (EXPECT(PyList_Reverse(inserts) < 0, 0)) {
            goto done;
        }
    }

    Py_ssize_t old_size = PyTuple_GET_SIZE(old);

    for (Py_ssize_t i = 0; i < num_del; ++i) {
        Py_ssize_t at = keys_bisect(old, PyList_GET_ITEM(deletes, i), self->ordering);
        if (EXPECT(at < 0, 0)) {
            goto done;
        }

        if (EXPECT(at == old_size, 0)) {
            status = 1;
            goto done;
        }

        int eq = PyObject_RichCompareBool(PyTuple_GET_ITEM(old, at), PyList_GET_ITEM(deletes, i), Py_EQ);
        if (EXPECT(eq < 0, 0)) {
            goto done;
        }

        if (EXPECT(!eq, 0)) {
            status = 1;
            goto done;
        }

        del_pos[i] = at;
    }

    for (Py_ssize_t i = 1; i < num_del; ++i) {
        Py_ssize_t value = del_pos[i];
        Py_ssize_t j = i;
        while (j > 0 && del_pos[j - 1] > value) {
            del_pos[j] = del_pos[j - 1];
            j--;
        }
        del_pos[j] = value;
    }

    for (Py_ssize_t i = 0; i < num_ins; ++i) {
        Py_ssize_t at = keys_bisect(old, PyList_GET_ITEM(inserts, i), self->ordering);
        if (EXPECT(at < 0, 0)) {
            goto done;
        }

        if (at < old_size) {
            int eq = PyObject_RichCompareBool(PyTuple_GET_ITEM(old, at), PyList_GET_ITEM(inserts, i), Py_EQ);
            if (EXPECT(eq < 0, 0)) {
                goto done;
            }

            if (EXPECT(eq, 0)) {
                status = 1;
                goto done;
            }
        }

        ins_pos[i] = at;
    }

    result = PyTuple_New(old_size - num_del + num_ins);
    if (EXPECT(!result, 0)) {
        goto done;
    }

    Py_ssize_t ri = 0;
    Py_ssize_t ii = 0;
    Py_ssize_t di = 0;

    for (Py_ssize_t oi = 0; oi <= old_size; ++oi) {
        while (ii < num_ins && ins_pos[ii] == oi) {
            PyTuple_SET_ITEM(result, ri++, Py_NewRef(PyList_GET_ITEM(inserts, ii)));
            ii++;
        }
        if (oi == old_size) {
            break;
        }
        if (di < num_del && del_pos[di] == oi) {
            di++;
            continue;
        }
        PyTuple_SET_ITEM(result, ri++, Py_NewRef(PyTuple_GET_ITEM(old, oi)));
    }

    if (EXPECT(self->version != version || self->pend_count != 0 || self->dirty, 0)
            || EXPECT(ri != old_size - num_del + num_ins, 0)
            || EXPECT(ri != PyDict_GET_SIZE(data), 0)) {
        status = 1;
        goto done;
    }

    Py_XSETREF(self->keys, result);
    result = NULL;
    status = 0;

done:
    if (status != 0) {
        // both paths need the dirty bit set
        escalate_to_dirty(self);
    }

    Py_XDECREF(result);
    Py_XDECREF(inserts);
    Py_XDECREF(deletes);
    Py_DECREF(old);
    Py_DECREF(data);
    for (Py_ssize_t i = 0; i < count; ++i) {
        Py_DECREF(pend[i].key);
    }
    return status;
}


/* internal helper function to update keys */
inline int update_keys(SortedDict *self) {
    if (!self->dirty && self->keys) {
        if (self->pend_count == 0) {
            return 0;
        }

        int applied = apply_pending(self);
        if (applied == 0) {
            return 0;
        }
        if (applied < 0) {
            return 1;
        }
        // the log escalated, fall through to the full sort
    }

    return full_sort(self);
}


static PyObject *build_items(SortedDict *self)
{
    if (EXPECT(update_keys(self), 0)) {
        return NULL;
    }

    // the book may be mutated while we build the snapshot, hold on to both
    // so a __hash__ or __eq__ on key type doesnt cause issues
    PyObject *keys = Py_NewRef(self->keys);
    PyObject *data = Py_NewRef(self->data);

    Py_ssize_t len = PyTuple_GET_SIZE(keys);
    if ((self->depth > 0) && (self->depth < len)) {
        len = self->depth;
    }

    PyObject *ret = PyList_New(len);
    if (EXPECT(!ret, 0)) {
        goto error;
    }

    for (Py_ssize_t i = 0; i < len; ++i) {
        PyObject *key = PyTuple_GET_ITEM(keys, i);
        PyObject *value = PyDict_GetItemWithError(data, key);
        if (EXPECT(!value, 0)) {
            if (!PyErr_Occurred()) {
                PyErr_SetObject(PyExc_KeyError, key);
            }
            Py_DECREF(ret);
            goto error;
        }

        PyObject *entry = PyTuple_New(2);
        if (EXPECT(!entry, 0)) {
            Py_DECREF(ret);
            goto error;
        }

        PyTuple_SET_ITEM(entry, 0, Py_NewRef(key));
        PyTuple_SET_ITEM(entry, 1, Py_NewRef(value));
        PyList_SET_ITEM(ret, i, entry);
    }

    Py_DECREF(keys);
    Py_DECREF(data);
    return ret;

error:
    Py_DECREF(keys);
    Py_DECREF(data);
    return NULL;
}


PyObject* SortedDict_keys(SortedDict *self, PyObject *Py_UNUSED(ignored))
{
    if (EXPECT(update_keys(self), 0)) {
        return NULL;
    }

    PyObject *ret = self->keys;

    if (self->depth) {
        ret = PySequence_GetSlice(ret, 0, self->depth);
    } else {
        Py_INCREF(ret);
    }

    return ret;
}


PyObject* SortedDict_index(SortedDict *self, PyObject *index)
{
    long i = PyLong_AsLong(index);
    if (EXPECT(PyErr_Occurred() != NULL, 0)) {
        return NULL;
    }

    if (EXPECT(update_keys(self), 0)) {
        return NULL;
    }

    // validate against max_depth
    Py_ssize_t len = PyTuple_GET_SIZE(self->keys);
    if ((self->depth > 0) && (self->depth < len)) {
        len = self->depth;
    }

    // negative indexing
    if (i < 0) {
        i += len;
    }

    if (EXPECT(i < 0 || i >= len, 0)) {
        PyErr_SetString(PyExc_IndexError, "index out of range");
        return NULL;
    }

    PyObject *key = Py_NewRef(PyTuple_GET_ITEM(self->keys, i));

    // borrowed reference
    PyObject *value = PyDict_GetItemWithError(self->data, key);
    if (EXPECT(!value, 0)) {
        if (!PyErr_Occurred()) {
            PyErr_SetObject(PyExc_KeyError, key);
        }
        Py_DECREF(key);
        return NULL;
    }

    PyObject *ret = PyTuple_New(2);
    if (EXPECT(!ret, 0)) {
        Py_DECREF(key);
        return NULL;
    }

    PyTuple_SET_ITEM(ret, 0, key);
    PyTuple_SET_ITEM(ret, 1, Py_NewRef(value));

    return ret;
}


static int convert_item(PyObject **obj, PyObject *from, PyObject *to)
{
    if (from) {
        int is_instance = PyObject_IsInstance(*obj, from);
        if (EXPECT(is_instance < 0, 0)) {
            return -1;
        }

        if (!is_instance) {
            return 0;
        }
    }

    PyObject *converted = PyObject_CallFunctionObjArgs(to, *obj, NULL);
    if (EXPECT(!converted, 0)) {
        return -1;
    }

    Py_SETREF(*obj, converted);
    return 0;
}


PyObject* SortedDict_todict(SortedDict *self, PyObject *unused, PyObject *kwargs)
{
    static char *kwlist[] = {"from_type", "to_type", NULL};
    PyObject *from = NULL;
    PyObject *to = NULL;

    if (!PyArg_ParseTupleAndKeywords(unused, kwargs, "|$OO", kwlist, &from, &to)) {
        return NULL;
    }

    if (!to) {
        // no conversion requested so build the dict straight from data
        if (EXPECT(update_keys(self), 0)) {
            return NULL;
        }

        PyObject *keys = Py_NewRef(self->keys);
        PyObject *data = Py_NewRef(self->data);

        Py_ssize_t len = PyTuple_GET_SIZE(keys);
        if ((self->depth > 0) && (self->depth < len)) {
            len = self->depth;
        }

        PyObject *ret = PyDict_New();
        if (EXPECT(!ret, 0)) {
            goto error;
        }

        for (Py_ssize_t i = 0; i < len; ++i) {
            PyObject *key = PyTuple_GET_ITEM(keys, i);
            PyObject *value = PyDict_GetItemWithError(data, key);
            if (EXPECT(!value, 0)) {
                if (!PyErr_Occurred()) {
                    PyErr_SetObject(PyExc_KeyError, key);
                }
                Py_DECREF(ret);
                goto error;
            }

            Py_INCREF(value);
            int failed = PyDict_SetItem(ret, key, value) < 0;
            Py_DECREF(value);

            if (EXPECT(failed, 0)) {
                Py_DECREF(ret);
                goto error;
            }
        }

        Py_DECREF(keys);
        Py_DECREF(data);
        return ret;

error:
        Py_DECREF(keys);
        Py_DECREF(data);
        return NULL;
    }

    PyObject *items = build_items(self);
    if (EXPECT(!items, 0)) {
        return NULL;
    }

    PyObject *ret = PyDict_New();
    if (EXPECT(!ret, 0)) {
        Py_DECREF(items);
        return NULL;
    }

    Py_ssize_t len = PyList_GET_SIZE(items);

    for(Py_ssize_t i = 0; i < len; ++i) {
        PyObject *entry = PyList_GET_ITEM(items, i);
        PyObject *key = Py_NewRef(PyTuple_GET_ITEM(entry, 0));
        PyObject *value = Py_NewRef(PyTuple_GET_ITEM(entry, 1));
        bool failed = false;

        if (to) {
            failed = convert_item(&key, from, to) || convert_item(&value, from, to);
        }

        if (!failed) {
            failed = PyDict_SetItem(ret, key, value) < 0;
        }

        Py_DECREF(key);
        Py_DECREF(value);

        if (EXPECT(failed, 0)) {
            Py_DECREF(ret);
            Py_DECREF(items);
            return NULL;
        }
    }

    Py_DECREF(items);
    return ret;
}


PyObject* SortedDict_tolist(SortedDict *self, PyObject *Py_UNUSED(ignored))
{
    return build_items(self);
}


static int truncate_to_depth(SortedDict *self)
{
    if (!self->depth) {
        return 0;
    }

    if (EXPECT(update_keys(self), 0)) {
        return -1;
    }

    Py_ssize_t size = PyTuple_GET_SIZE(self->keys);
    if (size <= self->depth) {
        return 0;
    }

    uint64_t version = self->version;
    PyObject *keys = Py_NewRef(self->keys);

    for (Py_ssize_t i = self->depth; i < size; ++i) {
        if (EXPECT(PyDict_DelItem(self->data, PyTuple_GET_ITEM(keys, i)) == -1, 0)) {
            escalate_to_dirty(self);
            Py_DECREF(keys);
            return -1;
        }
    }

    if (EXPECT(self->version == version, 1)) {
        // evictions only come off the tail
        PyObject *sliced = PyTuple_GetSlice(keys, 0, self->depth);
        Py_DECREF(keys);
        if (EXPECT(!sliced, 0)) {
            escalate_to_dirty(self);
            return -1;
        }
        Py_XSETREF(self->keys, sliced);
        return 0;
    }

    // a re-entrant mutation interleaved with the eviction, recompute
    Py_DECREF(keys);
    escalate_to_dirty(self);
    return update_keys(self) ? -1 : 0;
}


PyObject* SortedDict_truncate(SortedDict *self, PyObject *Py_UNUSED(ignored))
{
    if (EXPECT(truncate_to_depth(self), 0)) {
        return NULL;
    }

    Py_RETURN_NONE;
}


/* Sorted Dictionary Mapping Functions */
Py_ssize_t SortedDict_len(const SortedDict *self)
{
	int len = PyDict_Size(self->data);
    if (self->depth && self->depth < len) {
        return self->depth;
    }

    return len;
}

PyObject *SortedDict_getitem(SortedDict *self, PyObject *key)
{
    PyObject *ret = PyDict_GetItemWithError(self->data, key);
    if (ret) {
        Py_INCREF(ret);
        return ret;
    }

    if (EXPECT(!PyErr_Occurred(), 0)) {
        PyErr_SetString(PyExc_KeyError, "key does not exist");
    }

    return ret;
}

int SortedDict_setitem(SortedDict *self, PyObject *key, PyObject *value)
{
    bool cache_live = (!self->dirty && self->keys != NULL);
    uint64_t version = self->version;

    if (value) {
        Py_ssize_t before = PyDict_GET_SIZE(self->data);
        int ret = PyDict_SetItem(self->data, key, value);

        if (EXPECT(ret == -1, 0)) {
            return ret;
        }

        if (PyDict_GET_SIZE(self->data) == before && self->version == version) {
            // in place value update, the key set did not change
            return ret;
        }

        if (!cache_live) {
            self->dirty = true;
            self->version++;
        } else if (PyDict_GET_SIZE(self->data) == before + 1 && self->version == version) {
            log_append(self, PENDING_INSERT, key);
        } else {
            escalate_to_dirty(self);
        }

        if (EXPECT(self->truncate && truncate_to_depth(self), 0)) {
            return -1;
        }

        return ret;
    } else {
        // setitem also called for del (value will be null for deletes)
        int ret = PyDict_DelItem(self->data, key);
        if (ret != 0) {
            // a failed delete leaves the cache and log untouched
            return ret;
        }

        if (!cache_live) {
            self->dirty = true;
            self->version++;
        } else if (self->version == version) {
            log_append(self, PENDING_DELETE, key);
        } else {
            escalate_to_dirty(self);
        }

        return ret;
    }
}

/* Seq Functions */
int SortedDict_contains(const SortedDict *self, PyObject *value)
{
    return PySequence_Contains(self->data, value);
}

/* side iterator */
static void SortedDictIter_dealloc(SortedDictIter *self)
{
    PyObject_GC_UnTrack(self);
    Py_CLEAR(self->keys);
    Py_CLEAR(self->data);
    PyObject_GC_Del(self);
}


static int SortedDictIter_traverse(SortedDictIter *self, visitproc visit, void *arg)
{
    Py_VISIT(self->keys);
    Py_VISIT(self->data);

    return 0;
}


static int SortedDictIter_clear(SortedDictIter *self)
{
    Py_CLEAR(self->keys);
    Py_CLEAR(self->data);

    return 0;
}


static PyObject *SortedDictIter_next(SortedDictIter *self)
{
    if (self->index >= self->len) {
        return NULL;
    }

    PyObject *key = PyTuple_GET_ITEM(self->keys, self->index);

    if (!self->pairs) {
        self->index++;
        return Py_NewRef(key);
    }

    PyObject *value = PyDict_GetItemWithError(self->data, key);
    if (EXPECT(!value, 0)) {
        if (!PyErr_Occurred()) {
            // the level was deleted mid iteration so raise
            PyErr_SetObject(PyExc_KeyError, key);
        }
        return NULL;
    }

    Py_INCREF(value);
    PyObject *ret = PyTuple_New(2);
    if (EXPECT(!ret, 0)) {
        Py_DECREF(value);
        return NULL;
    }
    PyTuple_SET_ITEM(ret, 0, Py_NewRef(key));
    PyTuple_SET_ITEM(ret, 1, value);

    self->index++;
    return ret;
}


PyTypeObject SortedDictIterType = {
    PyVarObject_HEAD_INIT(NULL, 0)
    .tp_name = "order_book.side_iterator",
    .tp_doc = "iterator over the keys or (key, value) pairs of a SortedDict",
    .tp_basicsize = sizeof(SortedDictIter),
    .tp_itemsize = 0,
    .tp_flags = Py_TPFLAGS_DEFAULT | Py_TPFLAGS_HAVE_GC,
    .tp_dealloc = (destructor) SortedDictIter_dealloc,
    .tp_traverse = (traverseproc) SortedDictIter_traverse,
    .tp_clear = (inquiry) SortedDictIter_clear,
    .tp_iter = PyObject_SelfIter,
    .tp_iternext = (iternextfunc) SortedDictIter_next,
};


static PyObject *SortedDict_iter_new(SortedDict *self, bool pairs)
{
    if (EXPECT(update_keys(self), 0)) {
        return NULL;
    }

    SortedDictIter *it = PyObject_GC_New(SortedDictIter, &SortedDictIterType);
    if (EXPECT(!it, 0)) {
        return NULL;
    }

    it->keys = Py_NewRef(self->keys);
    it->data = Py_NewRef(self->data);
    it->index = 0;
    it->pairs = pairs;

    Py_ssize_t len = PyTuple_GET_SIZE(it->keys);
    if ((self->depth > 0) && (self->depth < len)) {
        len = self->depth;
    }
    it->len = len;

    PyObject_GC_Track(it);
    return (PyObject *)it;
}


PyObject *SortedDict_getiter(SortedDict *self)
{
    return SortedDict_iter_new(self, false);
}


PyObject* SortedDict_items(SortedDict *self, PyObject *Py_UNUSED(ignored))
{
    return SortedDict_iter_new(self, true);
}
