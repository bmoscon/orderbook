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
    PyObject *keys[SD_PENDING_MAX];
    uint16_t count = self->pend_count;

    for (uint16_t i = 0; i < count; ++i) {
        keys[i] = self->pend[i].key;
        self->pend[i].key = NULL;
    }

    self->pend_count = 0;
    self->version++;

    // empty before anything is released
    for (uint16_t i = 0; i < count; ++i) {
        Py_DECREF(keys[i]);
    }
}


void SortedDict_drop_key_cache(SortedDict *self)
{
    PyObject **arr = self->karr;
    Py_ssize_t len = self->k_len;

    self->karr = NULL;
    self->k_len = 0;
    Py_CLEAR(self->keys_tuple);

    if (arr) {
        for (Py_ssize_t i = 0; i < len; ++i) {
            Py_XDECREF(arr[i]);
        }
        PyMem_Free(arr);
    }
}


// install a freshly built key array, releasing the previous one afterwards
static void karr_install(SortedDict *self, PyObject **arr, Py_ssize_t len, PyObject *tuple)
{
    PyObject **prev = self->karr;
    Py_ssize_t prev_len = self->k_len;
    PyObject *prev_tuple = self->keys_tuple;

    self->karr = arr;
    self->k_len = len;
    self->keys_tuple = tuple;

    Py_XDECREF(prev_tuple);
    if (prev) {
        for (Py_ssize_t i = 0; i < prev_len; ++i) {
            Py_XDECREF(prev[i]);
        }
        PyMem_Free(prev);
    }
}


// borrowed ref to an immutable tuple snapshot of the key cache, cached until the key set changes
static PyObject *karr_materialize(SortedDict *self)
{
    if (self->keys_tuple) {
        return self->keys_tuple;
    }

    PyObject *t = PyTuple_New(self->k_len);
    if (EXPECT(!t, 0)) {
        return NULL;
    }

    for (Py_ssize_t i = 0; i < self->k_len; ++i) {
        PyTuple_SET_ITEM(t, i, Py_NewRef(self->karr[i]));
    }

    self->keys_tuple = t;
    return t;
}


// a new tuple of the first 'want' cached keys (or fewer), for consumers that only need the top of the book
PyObject *SortedDict_key_window(SortedDict *self, Py_ssize_t want)
{
    Py_ssize_t n = (self->k_len < want) ? self->k_len : want;
    PyObject *t = PyTuple_New(n);

    if (EXPECT(!t, 0)) {
        return NULL;
    }

    for (Py_ssize_t i = 0; i < n; ++i) {
        PyTuple_SET_ITEM(t, i, Py_NewRef(self->karr[i]));
    }

    return t;
}


static void escalate_to_dirty(SortedDict *self)
{
    self->dirty = true;
    SortedDict_flush_pending(self);
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
    SortedDict_drop_key_cache(self);
    Py_CLEAR(self->data);
    Py_TYPE(self)->tp_free((PyObject *) self);
}


int SortedDict_traverse(SortedDict *self, visitproc visit, void *arg)
{
    Py_VISIT(self->data);
    Py_VISIT(self->keys_tuple);

    for (Py_ssize_t i = 0; i < self->k_len; ++i) {
        Py_VISIT(self->karr[i]);
    }

    for (uint16_t i = 0; i < self->pend_count; ++i) {
        Py_VISIT(self->pend[i].key);
    }

    return 0;
}


int SortedDict_clear(SortedDict *self)
{
    SortedDict_flush_pending(self);
    SortedDict_drop_key_cache(self);

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
        self->karr = NULL;
        self->k_len = 0;
        self->keys_tuple = NULL;
        self->dirty = false;
        self->depth = 0;
        self->truncate = false;
        self->pend_count = 0;
        self->version = 0;
    }

    return (PyObject *) self;
}


static int locked_init(SortedDict *self, PyObject *args, PyObject *kwds)
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

        PyObject *previous = self->data;
        self->data = copy;
        self->dirty = true;
        SortedDict_drop_key_cache(self);
        SortedDict_flush_pending(self);
        Py_XDECREF(previous);
    }


    if (kwds && PyDict_Check(kwds) && PyDict_Size(kwds) > 0) {
        // borrowed refs, getItemString returns NULL when not found
        PyObject *max_depth = PyDict_GetItemString(kwds, "max_depth");
        PyObject *truncate = PyDict_GetItemString(kwds, "truncate");
        PyObject *ordering_arg = PyDict_GetItemString(kwds, "ordering");

        if (max_depth) {
            if (PyLong_Check(max_depth)) {
                long depth = PyLong_AsLong(max_depth);
                
                if (depth == -1 && PyErr_Occurred()) {
                    return -1;
                }

                if (depth < 1 || depth > INT_MAX) {
                    PyErr_SetString(PyExc_ValueError, "max_depth must be greater than 0");
                    return -1;
                }

                self->depth = (int) depth;

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

        enum Ordering requested;

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
                requested = DESCENDING;
            } else if (strcmp(value, "ASC") == 0) {
                requested = ASCENDING;
            } else {
                PyErr_SetString(PyExc_ValueError, "ordering must be one of ASC or DESC");
                return -1;
            }
        } else if (self->ordering == INVALID_ORDERING) {
            // default is ascending
            requested = ASCENDING;
        } else {
            requested = self->ordering;
        }

        if (requested != self->ordering) {
            self->ordering = requested;
            escalate_to_dirty(self);
            SortedDict_drop_key_cache(self);
        }
    }

    if (self->truncate && self->data) {
        if (EXPECT(truncate_to_depth(self), 0)) {
            return -1;
        }
    }

    return 0;
}


int SortedDict_init(SortedDict *self, PyObject *args, PyObject *kwds)
{
    int ret;
    Py_BEGIN_CRITICAL_SECTION(self);
    ret = locked_init(self, args, kwds);
    Py_END_CRITICAL_SECTION();
    return ret;
}


// bisect over the live key array. every compare can reenter and swap the
// array out from under us, so the version is checked after each one.
// ret >= 0 position, -1 exception, -2 the book mutated mid-search
static Py_ssize_t keys_bisect(SortedDict *self, uint64_t version, PyObject *key)
{
    Py_ssize_t lo = 0;
    Py_ssize_t hi = self->k_len;
    int op = (self->ordering == DESCENDING) ? Py_GT : Py_LT;

    while (lo < hi) {
        Py_ssize_t mid = lo + ((hi - lo) >> 1);
        PyObject *probe = Py_NewRef(self->karr[mid]);
        int before = PyObject_RichCompareBool(probe, key, op);
        Py_DECREF(probe);

        if (EXPECT(before < 0, 0)) {
            return -1;
        }

        if (EXPECT(self->version != version, 0)) {
            return -2;
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

        PyObject *data = Py_NewRef(self->data);
        PyObject *keys = PyDict_Keys(data);
        Py_DECREF(data);
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

        PyObject *tuple = PySequence_Tuple(keys);
        Py_DECREF(keys);
        if (EXPECT(!tuple, 0)) {
            return 1;
        }

        Py_ssize_t n = PyTuple_GET_SIZE(tuple);
        PyObject **arr = PyMem_New(PyObject *, n > 0 ? n : 1);
        if (EXPECT(!arr, 0)) {
            Py_DECREF(tuple);
            PyErr_NoMemory();
            return 1;
        }

        memcpy(arr, PySequence_Fast_ITEMS(tuple), n * sizeof(PyObject *));
        for (Py_ssize_t i = 0; i < n; ++i) {
            Py_INCREF(arr[i]);
        }

        if (EXPECT(self->version == version, 1)) {
            self->dirty = false;
            karr_install(self, arr, n, tuple);
            return 0;
        }

        if (attempt == 3) {
            // a comparator keeps mutating the book, so leave dirty
            karr_install(self, arr, n, tuple);
            return 0;
        }

        for (Py_ssize_t i = 0; i < n; ++i) {
            Py_DECREF(arr[i]);
        }
        PyMem_Free(arr);
        Py_DECREF(tuple);
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

    // the book may be mutated by any comparison or hash below so hold the ref.
    // the key array needs no hold: every python call is followed by a version
    // check before the array is touched again
    PyObject *data = Py_NewRef(self->data);
    PyObject *inserts = NULL;
    PyObject *deletes = NULL;
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

    if (EXPECT(self->version != version, 0)) {
        status = 1;
        goto done;
    }

    Py_ssize_t old_size = self->k_len;

    for (Py_ssize_t i = 0; i < num_del; ++i) {
        Py_ssize_t at = keys_bisect(self, version, PyList_GET_ITEM(deletes, i));
        if (EXPECT(at < 0, 0)) {
            status = (at == -2) ? 1 : -1;
            goto done;
        }

        if (EXPECT(at == old_size, 0)) {
            status = 1;
            goto done;
        }

        PyObject *probe = Py_NewRef(self->karr[at]);
        int eq = PyObject_RichCompareBool(probe, PyList_GET_ITEM(deletes, i), Py_EQ);
        Py_DECREF(probe);
        if (EXPECT(eq < 0, 0)) {
            goto done;
        }
        if (EXPECT(self->version != version || !eq, 0)) {
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
        Py_ssize_t at = keys_bisect(self, version, PyList_GET_ITEM(inserts, i));
        if (EXPECT(at < 0, 0)) {
            status = (at == -2) ? 1 : -1;
            goto done;
        }

        if (at < old_size) {
            PyObject *probe = Py_NewRef(self->karr[at]);
            int eq = PyObject_RichCompareBool(probe, PyList_GET_ITEM(inserts, i), Py_EQ);
            Py_DECREF(probe);
            if (EXPECT(eq < 0, 0)) {
                goto done;
            }
            if (EXPECT(self->version != version || eq, 0)) {
                status = 1;
                goto done;
            }
        }

        ins_pos[i] = at;
    }

    Py_ssize_t new_size = old_size - num_del + num_ins;

    if (EXPECT(self->version != version || self->pend_count != 0 || self->dirty, 0) || EXPECT(new_size != PyDict_GET_SIZE(data), 0)) {
        status = 1;
        goto done;
    }

    for (Py_ssize_t i = 1; i < num_del; ++i) {
        if (EXPECT(del_pos[i] <= del_pos[i - 1], 0)) {
            status = 1;
            goto done;
        }
    }

    for (Py_ssize_t i = 1; i < num_ins; ++i) {
        if (EXPECT(ins_pos[i] < ins_pos[i - 1], 0)) {
            status = 1;
            goto done;
        }
    }

    // merge by moving surviving pointers into a fresh array. survivors
    // transfer their reference, so there is no refcount traffic and
    // no O(N) tuple to build and destroy
    {
        PyObject **scratch = PyMem_New(PyObject *, new_size > 0 ? new_size : 1);
        if (EXPECT(!scratch, 0)) {
            PyErr_NoMemory();
            goto done;
        }

        PyObject *dropped[SD_PENDING_MAX];
        Py_ssize_t ri = 0;
        Py_ssize_t ii = 0;
        Py_ssize_t di = 0;
        Py_ssize_t oi = 0;

        // copy the untouched runs between change positions with memcpy
        while (1) {
            Py_ssize_t next_ins = (ii < num_ins) ? ins_pos[ii] : old_size;
            Py_ssize_t next_del = (di < num_del) ? del_pos[di] : old_size;
            Py_ssize_t stop = (next_ins < next_del) ? next_ins : next_del;

            if (stop > oi) {
                memcpy(scratch + ri, self->karr + oi, (stop - oi) * sizeof(PyObject *));
                ri += stop - oi;
                oi = stop;
            }

            if (ii < num_ins && ins_pos[ii] == oi) {
                scratch[ri++] = Py_NewRef(PyList_GET_ITEM(inserts, ii));
                ii++;
                continue;
            }

            if (di < num_del && del_pos[di] == oi) {
                dropped[di] = self->karr[oi];
                di++;
                oi++;
                continue;
            }

            break;
        }

        if (EXPECT(ri != new_size, 0)) {
            // should be unreachable given the checks above
            for (Py_ssize_t i = 0; i < ii; ++i) {
                Py_DECREF(PyList_GET_ITEM(inserts, i));
            }

            PyMem_Free(scratch);
            status = 1;
            goto done;
        }

        PyObject **prev = self->karr;
        self->karr = scratch;
        self->k_len = new_size;
        self->version++;
        Py_CLEAR(self->keys_tuple);
        PyMem_Free(prev);

        for (Py_ssize_t i = 0; i < num_del; ++i) {
            Py_DECREF(dropped[i]);
        }
    }
    status = 0;

done:
    if (status != 0) {
        // both paths need the dirty bit set
        escalate_to_dirty(self);
    }

    Py_XDECREF(inserts);
    Py_XDECREF(deletes);
    Py_DECREF(data);

    for (Py_ssize_t i = 0; i < count; ++i) {
        Py_DECREF(pend[i].key);
    }

    return status;
}


/* internal helper function to update keys */
inline int update_keys(SortedDict *self) {
    while (true) {
        if (!self->dirty && self->karr) {
            if (self->pend_count == 0) {
                return 0;
            }

            int applied = apply_pending(self);
            if (applied < 0) {
                return 1;
            }

            if (applied == 0) {
                continue;
            }
        }

        if (full_sort(self)) {
            return 1;
        }

        return 0;
    }
}


static PyObject *build_items_once(SortedDict *self, bool *moved)
{
    *moved = false;

    if (EXPECT(update_keys(self), 0)) {
        return NULL;
    }

    PyObject *keys = karr_materialize(self);
    if (EXPECT(!keys, 0)) {
        return NULL;
    }

    Py_INCREF(keys);
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
        PyObject *value;
        int found = PyDict_GetItemRef(data, key, &value);

        if (EXPECT(found <= 0, 0)) {
            if (found == 0) {
                escalate_to_dirty(self);
                *moved = true;
                PyErr_SetObject(PyExc_KeyError, key);
            }
            Py_DECREF(ret);
            goto error;
        }

        PyObject *entry = PyTuple_New(2);
        if (EXPECT(!entry, 0)) {
            Py_DECREF(value);
            Py_DECREF(ret);
            goto error;
        }

        PyTuple_SET_ITEM(entry, 0, Py_NewRef(key));
        PyTuple_SET_ITEM(entry, 1, value);
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


static PyObject *build_items(SortedDict *self)
{
    bool moved;
    PyObject *ret = build_items_once(self, &moved);

    for (int attempt = 0; !ret && moved && attempt < SD_READ_RETRIES; ++attempt) {
        PyErr_Clear();
        ret = build_items_once(self, &moved);
    }

    return ret;
}


static PyObject *locked_keys(SortedDict *self)
{
    if (EXPECT(update_keys(self), 0)) {
        return NULL;
    }

    PyObject *ret = karr_materialize(self);
    if (EXPECT(!ret, 0)) {
        return NULL;
    }

    if (self->depth > 0) {
        return PySequence_GetSlice(ret, 0, self->depth);
    }

    return Py_NewRef(ret);
}


PyObject* SortedDict_keys(SortedDict *self, PyObject *Py_UNUSED(ignored))
{
    PyObject *ret;
    Py_BEGIN_CRITICAL_SECTION(self);
    ret = locked_keys(self);
    Py_END_CRITICAL_SECTION();
    return ret;
}


// answer index(0) from the clean cache plus the pending log without merging
static int peek_best(SortedDict *self, PyObject **out)
{
    uint64_t version = self->version;
    PyObject *data = Py_NewRef(self->data);
    PyObject *best = NULL;
    Py_ssize_t count = self->pend_count;
    int status = 1;
    bool have_deletes = false;

    for (Py_ssize_t p = 0; p < count; ++p) {
        if (self->pend[p].op == PENDING_DELETE) {
            have_deletes = true;
            break;
        }
    }

    Py_ssize_t old_size = self->k_len;
    if (!have_deletes) {
        if (old_size) {
            best = Py_NewRef(self->karr[0]);
        }
    } else {
        Py_ssize_t limit = (count + 1 < old_size) ? count + 1 : old_size;
        for (Py_ssize_t j = 0; j < limit; ++j) {
            PyObject *cand = Py_NewRef(self->karr[j]);
            int in = PyDict_Contains(data, cand);

            if (EXPECT(in < 0, 0)) {
                Py_DECREF(cand);
                status = -1;
                goto done;
            }

            if (EXPECT(self->version != version, 0)) {
                Py_DECREF(cand);
                goto done;
            }

            if (in) {
                best = cand;
                break;
            }

            Py_DECREF(cand);
            if (EXPECT(self->version != version, 0)) {
                goto done;
            }
        }
    }

    // a pending insert can beat the best surviving old key
    for (Py_ssize_t p = 0; p < count; ++p) {
        // the previous round ended in a release
        if (EXPECT(self->version != version, 0)) {
            goto done;
        }

        if (self->pend[p].op != PENDING_INSERT) {
            continue;
        }

        PyObject *cand = Py_NewRef(self->pend[p].key);
        if (have_deletes) {
            int in = PyDict_Contains(data, cand);

            if (EXPECT(in < 0, 0)) {
                Py_DECREF(cand);
                status = -1;
                goto done;
            }

            if (EXPECT(self->version != version, 0)) {
                Py_DECREF(cand);
                goto done;
            }

            if (!in) {
                Py_DECREF(cand);
                continue;
            }
        }

        if (!best) {
            best = cand;
            continue;
        }

        int better = PyObject_RichCompareBool(cand, best, self->ordering == DESCENDING ? Py_GT : Py_LT);
        if (EXPECT(better < 0, 0)) {
            Py_DECREF(cand);
            status = -1;
            goto done;
        }

        if (EXPECT(self->version != version, 0)) {
            Py_DECREF(cand);
            goto done;
        }

        if (better) {
            Py_SETREF(best, cand);
        } else {
            Py_DECREF(cand);
        }
    }

    if (!best) {
        goto done;
    }

    PyObject *value;
    int found = PyDict_GetItemRef(data, best, &value);
    if (EXPECT(found <= 0, 0)) {
        status = (found < 0) ? -1 : 1;
        goto done;
    }

    if (EXPECT(self->version != version, 0)) {
        Py_DECREF(value);
        goto done;
    }

    PyObject *ret = PyTuple_New(2);
    if (EXPECT(!ret, 0)) {
        Py_DECREF(value);
        status = -1;
        goto done;
    }

    PyTuple_SET_ITEM(ret, 0, Py_NewRef(best));
    PyTuple_SET_ITEM(ret, 1, value);
    *out = ret;
    status = 0;

done:
    Py_XDECREF(best);
    Py_DECREF(data);
    return status;
}


static PyObject *index_once(SortedDict *self, long i, bool *moved)
{
    *moved = false;

    if (EXPECT(update_keys(self), 0)) {
        return NULL;
    }

    // validate against max_depth
    Py_ssize_t len = self->k_len;
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

    PyObject *key = Py_NewRef(self->karr[i]);
    PyObject *value;
    PyObject *data = Py_NewRef(self->data);
    int found = PyDict_GetItemRef(data, key, &value);
    Py_DECREF(data);
    if (EXPECT(found <= 0, 0)) {
        if (found == 0) {
            escalate_to_dirty(self);
            *moved = true;
            PyErr_SetObject(PyExc_KeyError, key);
        }
        Py_DECREF(key);
        return NULL;
    }

    PyObject *ret = PyTuple_New(2);
    if (EXPECT(!ret, 0)) {
        Py_DECREF(value);
        Py_DECREF(key);
        return NULL;
    }

    PyTuple_SET_ITEM(ret, 0, key);
    PyTuple_SET_ITEM(ret, 1, value);

    return ret;
}


static PyObject *locked_index(SortedDict *self, long i)
{
    if (i == 0 && !self->dirty && self->karr && self->pend_count) {
        PyObject *ret = NULL;
        int status = peek_best(self, &ret);
        if (status <= 0) {
            return ret;
        }
    }

    bool moved;
    PyObject *ret = index_once(self, i, &moved);

    for (int attempt = 0; !ret && moved && attempt < SD_READ_RETRIES; ++attempt) {
        PyErr_Clear();
        ret = index_once(self, i, &moved);
    }

    return ret;
}


PyObject* SortedDict_index(SortedDict *self, PyObject *index)
{
    long i = PyLong_AsLong(index);
    if (EXPECT(PyErr_Occurred() != NULL, 0)) {
        return NULL;
    }

    PyObject *ret;
    Py_BEGIN_CRITICAL_SECTION(self);
    ret = locked_index(self, i);
    Py_END_CRITICAL_SECTION();
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


static PyObject *todict_once(SortedDict *self, PyObject *from, PyObject *to, bool *moved)
{
    *moved = false;

    if (EXPECT(update_keys(self), 0)) {
        return NULL;
    }

    // the book may be mutated by a key hash or a conversion callback below,
    // hold both so the snapshot stays intact
    PyObject *keys = karr_materialize(self);
    if (EXPECT(!keys, 0)) {
        return NULL;
    }
    Py_INCREF(keys);
    PyObject *data = Py_NewRef(self->data);
    PyObject *values = NULL;
    PyObject *ret = NULL;

    Py_ssize_t len = PyTuple_GET_SIZE(keys);
    if ((self->depth > 0) && (self->depth < len)) {
        len = self->depth;
    }

    if (to) {
        // conversion runs arbitrary Python that may mutate the book so take the value
        // snapshot before converting anything
        values = PyList_New(len);
        if (EXPECT(!values, 0)) {
            goto error;
        }

        for (Py_ssize_t i = 0; i < len; ++i) {
            PyObject *value;
            int found = PyDict_GetItemRef(data, PyTuple_GET_ITEM(keys, i), &value);

            if (EXPECT(found <= 0, 0)) {
                if (found == 0) {
                    escalate_to_dirty(self);
                    *moved = true;
                    PyErr_SetObject(PyExc_KeyError, PyTuple_GET_ITEM(keys, i));
                }
                goto error;
            }

            PyList_SET_ITEM(values, i, value);
        }
    }

    ret = PyDict_New();
    if (EXPECT(!ret, 0)) {
        goto error;
    }

    for (Py_ssize_t i = 0; i < len; ++i) {
        // borrowed from the held keys tuple, which outlives every call below
        PyObject *key = PyTuple_GET_ITEM(keys, i);
        PyObject *value;
        bool failed;

        if (to) {
            Py_INCREF(key);
            value = Py_NewRef(PyList_GET_ITEM(values, i));
            failed = convert_item(&key, from, to) || convert_item(&value, from, to);

            if (!failed) {
                failed = PyDict_SetItem(ret, key, value) < 0;
            }

            Py_DECREF(key);
            Py_DECREF(value);
        } else {
            int found = PyDict_GetItemRef(data, key, &value);
            if (EXPECT(found <= 0, 0)) {
                if (found == 0) {
                    escalate_to_dirty(self);
                    *moved = true;
                    PyErr_SetObject(PyExc_KeyError, key);
                }
                goto error;
            }

            // the insert hashes the key, which can drop the book ref
            failed = PyDict_SetItem(ret, key, value) < 0;
            Py_DECREF(value);
        }

        if (EXPECT(failed, 0)) {
            goto error;
        }
    }

    Py_XDECREF(values);
    Py_DECREF(keys);
    Py_DECREF(data);
    return ret;

error:
    Py_XDECREF(ret);
    Py_XDECREF(values);
    Py_DECREF(keys);
    Py_DECREF(data);
    return NULL;
}


PyObject* locked_SortedDict_todict(SortedDict *self, PyObject *from, PyObject *to)
{
    bool moved;
    PyObject *ret = todict_once(self, from, to, &moved);

    for (int attempt = 0; !ret && moved && attempt < SD_READ_RETRIES; ++attempt) {
        PyErr_Clear();
        ret = todict_once(self, from, to, &moved);
    }

    return ret;
}


PyObject* SortedDict_todict(SortedDict *self, PyObject *unused, PyObject *kwargs)
{
    static char *kwlist[] = {"from_type", "to_type", NULL};
    PyObject *from = NULL;
    PyObject *to = NULL;

    if (!PyArg_ParseTupleAndKeywords(unused, kwargs, "|$OO", kwlist, &from, &to)) {
        return NULL;
    }

    PyObject *ret;
    Py_BEGIN_CRITICAL_SECTION(self);
    ret = locked_SortedDict_todict(self, from, to);
    Py_END_CRITICAL_SECTION();
    return ret;
}


PyObject* SortedDict_tolist(SortedDict *self, PyObject *Py_UNUSED(ignored))
{
    PyObject *ret;
    Py_BEGIN_CRITICAL_SECTION(self);
    ret = build_items(self);
    Py_END_CRITICAL_SECTION();
    return ret;
}


static int truncate_to_depth(SortedDict *self)
{
    if (!self->depth) {
        return 0;
    }

    if (EXPECT(update_keys(self), 0)) {
        return -1;
    }

    const Py_ssize_t depth = self->depth;
    Py_ssize_t size = self->k_len;
    if (size <= depth) {
        return 0;
    }

    uint64_t version = self->version;
    // hold an immutable snapshot for the delete loop
    PyObject *keys = karr_materialize(self);
    if (EXPECT(!keys, 0)) {
        escalate_to_dirty(self);
        return -1;
    }
    Py_INCREF(keys);
    PyObject *data = Py_NewRef(self->data);

    for (Py_ssize_t i = depth; i < size; ++i) {
        if (EXPECT(PyDict_DelItem(data, PyTuple_GET_ITEM(keys, i)) == -1, 0)) {
            if (!PyErr_ExceptionMatches(PyExc_KeyError)) {
                escalate_to_dirty(self);
                Py_DECREF(data);
                Py_DECREF(keys);
                return -1;
            }
            PyErr_Clear();
        }
    }

    Py_DECREF(data);

    if (EXPECT(self->version == version, 1)) {
        // evictions only come off the tail: shrink in place. the evicted
        // refs are released only after the array is consistent
        PyObject **evicted = PyMem_New(PyObject *, size - depth);
        if (EXPECT(!evicted, 0)) {
            Py_DECREF(keys);
            escalate_to_dirty(self);
            PyErr_NoMemory();
            return -1;
        }

        for (Py_ssize_t i = depth; i < size; ++i) {
            evicted[i - depth] = self->karr[i];
        }

        self->k_len = depth;
        self->version++;
        Py_CLEAR(self->keys_tuple);

        for (Py_ssize_t i = 0; i < size - depth; ++i) {
            Py_DECREF(evicted[i]);
        }

        PyMem_Free(evicted);
        Py_DECREF(keys);
        return 0;
    }

    // a re-entrant mutation interleaved with the eviction, recompute
    Py_DECREF(keys);
    escalate_to_dirty(self);
    return update_keys(self) ? -1 : 0;
}


PyObject* SortedDict_truncate(SortedDict *self, PyObject *Py_UNUSED(ignored))
{
    int ret;
    Py_BEGIN_CRITICAL_SECTION(self);
    ret = truncate_to_depth(self);
    Py_END_CRITICAL_SECTION();

    if (EXPECT(ret, 0)) {
        return NULL;
    }

    Py_RETURN_NONE;
}


/* Sorted Dictionary Mapping Functions */
Py_ssize_t locked_SortedDict_len(const SortedDict *self)
{
    Py_ssize_t len = PyDict_GET_SIZE(self->data);
    if (self->depth > 0 && self->depth < len) {
        return self->depth;
    }

    return len;
}


Py_ssize_t SortedDict_len(const SortedDict *self)
{
    Py_ssize_t ret;
    Py_BEGIN_CRITICAL_SECTION(self);
    ret = locked_SortedDict_len(self);
    Py_END_CRITICAL_SECTION();
    return ret;
}


PyObject *SortedDict_get_data(SortedDict *self, void *Py_UNUSED(closure))
{
    PyObject *ret;
    Py_BEGIN_CRITICAL_SECTION(self);
    ret = Py_NewRef(self->data);
    Py_END_CRITICAL_SECTION();
    return ret;
}

PyObject *SortedDict_getitem(SortedDict *self, PyObject *key)
{
    PyObject *ret;
    int found;

    Py_BEGIN_CRITICAL_SECTION(self);
    PyObject *data = Py_NewRef(self->data);
    found = PyDict_GetItemRef(data, key, &ret);
    Py_DECREF(data);
    Py_END_CRITICAL_SECTION();

    if (EXPECT(found == 0, 0)) {
        PyErr_SetString(PyExc_KeyError, "key does not exist");
    }

    return ret;
}


static int locked_setitem(SortedDict *self, PyObject *key, PyObject *value)
{
    bool cache_live = (!self->dirty && self->karr != NULL);
    uint64_t version = self->version;

    if (EXPECT(cache_live && self->pend_count == SD_PENDING_MAX, 0)) {
        if (apply_pending(self) != 0 && PyErr_Occurred()) {
            PyErr_Clear();
        }
        cache_live = (!self->dirty && self->karr != NULL);
        version = self->version;
    }

    PyObject *data = Py_NewRef(self->data);

    if (value) {
        Py_ssize_t before = PyDict_GET_SIZE(data);
        int ret = PyDict_SetItem(data, key, value);

        if (EXPECT(ret == -1, 0)) {
            Py_DECREF(data);
            return ret;
        }

        if (PyDict_GET_SIZE(data) == before && self->version == version) {
            // in place value update, the key set did not change
            Py_DECREF(data);
            return ret;
        }

        if (!cache_live) {
            self->dirty = true;
            self->version++;
        } else if (PyDict_GET_SIZE(data) == before + 1 && self->version == version) {
            log_append(self, PENDING_INSERT, key);
        } else {
            escalate_to_dirty(self);
        }

        Py_DECREF(data);

        if (EXPECT(self->truncate && truncate_to_depth(self), 0)) {
            return -1;
        }

        return ret;
    } else {
        // setitem also called for del (value will be null for deletes)
        uint64_t claimed = ++self->version;

        int ret = PyDict_DelItem(data, key);
        Py_DECREF(data);
        if (ret != 0) {
            // a failed delete leaves the dict as it was so needs to be rebuilt
            escalate_to_dirty(self);
            return ret;
        }

        if (!cache_live || self->version != claimed) {
            escalate_to_dirty(self);
        } else {
            log_append(self, PENDING_DELETE, key);
        }

        return ret;
    }
}

int SortedDict_setitem(SortedDict *self, PyObject *key, PyObject *value)
{
    int ret;
    Py_BEGIN_CRITICAL_SECTION(self);
    ret = locked_setitem(self, key, value);
    Py_END_CRITICAL_SECTION();
    return ret;
}


/* Seq Functions */
int SortedDict_contains(const SortedDict *self, PyObject *value)
{
    int ret;
    Py_BEGIN_CRITICAL_SECTION(self);
    PyObject *data = Py_NewRef(self->data);
    ret = PyDict_Contains(data, value);
    Py_DECREF(data);
    Py_END_CRITICAL_SECTION();
    return ret;
}

/* side iterator */
static void SortedDictIter_dealloc(SortedDictIter *self)
{
    PyObject_GC_UnTrack(self);
    Py_CLEAR(self->keys);
    Py_CLEAR(self->data);
    Py_CLEAR(self->owner);
    PyObject_GC_Del(self);
}


static int SortedDictIter_traverse(SortedDictIter *self, visitproc visit, void *arg)
{
    Py_VISIT(self->keys);
    Py_VISIT(self->data);
    Py_VISIT(self->owner);

    return 0;
}


static int SortedDictIter_clear(SortedDictIter *self)
{
    Py_CLEAR(self->keys);
    Py_CLEAR(self->data);
    Py_CLEAR(self->owner);

    return 0;
}


static PyObject *SortedDictIter_next(SortedDictIter *self)
{
    Py_ssize_t index;

    Py_BEGIN_CRITICAL_SECTION(self);
    index = self->index;
    if (index < self->len) {
        self->index = index + 1;
    }
    Py_END_CRITICAL_SECTION();

    if (index >= self->len) {
        return NULL;
    }

    PyObject *key = PyTuple_GET_ITEM(self->keys, index);

    if (!self->pairs) {
        return Py_NewRef(key);
    }

    PyObject *value;
    int found;
    Py_BEGIN_CRITICAL_SECTION(self->owner);
    found = PyDict_GetItemRef(self->data, key, &value);
    Py_END_CRITICAL_SECTION();
    if (EXPECT(found <= 0, 0)) {
        if (found == 0) {
            // the level was deleted mid iteration so raise
            PyErr_SetObject(PyExc_KeyError, key);
        }
        return NULL;
    }

    PyObject *ret = PyTuple_New(2);
    if (EXPECT(!ret, 0)) {
        Py_DECREF(value);
        return NULL;
    }
    PyTuple_SET_ITEM(ret, 0, Py_NewRef(key));
    PyTuple_SET_ITEM(ret, 1, value);

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


static PyObject *locked_iter_new(SortedDict *self, bool pairs)
{
    if (EXPECT(update_keys(self), 0)) {
        return NULL;
    }

    PyObject *snapshot = karr_materialize(self);
    if (EXPECT(!snapshot, 0)) {
        return NULL;
    }

    SortedDictIter *it = PyObject_GC_New(SortedDictIter, &SortedDictIterType);
    if (EXPECT(!it, 0)) {
        return NULL;
    }

    it->keys = Py_NewRef(snapshot);
    it->data = Py_NewRef(self->data);
    it->owner = Py_NewRef((PyObject *)self);
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


static PyObject *SortedDict_iter_new(SortedDict *self, bool pairs)
{
    PyObject *ret;
    Py_BEGIN_CRITICAL_SECTION(self);
    ret = locked_iter_new(self, pairs);
    Py_END_CRITICAL_SECTION();
    return ret;
}


PyObject *SortedDict_getiter(SortedDict *self)
{
    return SortedDict_iter_new(self, false);
}


PyObject* SortedDict_items(SortedDict *self, PyObject *Py_UNUSED(ignored))
{
    return SortedDict_iter_new(self, true);
}
