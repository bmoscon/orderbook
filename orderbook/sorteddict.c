/*
Copyright (C) 2020-2026  Bryant Moscon - bmoscon@gmail.com

Please see the LICENSE file for the terms and conditions
associated with this software.
*/
#include "sorteddict.h"
#include "utils.h"


/*
The sorted key cache (karr) is kept incrementally. Inserts and deletes made
between reads are logged in pend and merged into the cache by the next read.
when the log fills, or something happens that the log cannot describe, dirty
is set and the next read sorts the dict's keys from scratch.

Any Python call (compare, hash, str, finalizer) can reenter this object, and on
a free-threaded build a call that blocks releases the critical section to other
threads. so, version is bumped by every change to the key set or the cache,
every Python call is followed by a version check before the cache is touched
again, state is published before references are released, and self->data is
held across dict calls because a reentrant assignment can swap it out.
*/


static PyObject *SortedDict_iter_new(SortedDict *self, bool pairs);


static void flush_pending(SortedDict *self)
{
    PyObject *keys[SD_PENDING_MAX];
    uint16_t count = self->pend_count;

    for (uint16_t i = 0; i < count; ++i) {
        keys[i] = self->pend[i].key;
    }

    self->pend_count = 0;
    self->version++;

    // empty before anything is released
    for (uint16_t i = 0; i < count; ++i) {
        Py_DECREF(keys[i]);
    }
}


// only a full sort can rebuild the cache now
void SortedDict_invalidate(SortedDict *self)
{
    self->dirty = true;
    flush_pending(self);
}


// install a key array (NULL drops the cache), releasing the previous one afterwards
static void karr_install(SortedDict *self, PyObject **arr, Py_ssize_t len)
{
    PyObject **prev = self->karr;
    Py_ssize_t prev_len = self->k_len;

    self->karr = arr;
    self->k_len = len;

    if (prev) {
        for (Py_ssize_t i = 0; i < prev_len; ++i) {
            Py_DECREF(prev[i]);
        }
        PyMem_Free(prev);
    }
}


// a tuple of the first 'want' cached keys (or fewer), an immutable snapshot for readers
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


// how much of the cache max_depth lets a reader see
static Py_ssize_t visible_len(const SortedDict *self)
{
    return (self->depth > 0 && self->depth < self->k_len) ? self->depth : self->k_len;
}


// (key, value), stealing both references
static PyObject *make_pair(PyObject *key, PyObject *value)
{
    PyObject *pair = PyTuple_New(2);

    if (EXPECT(!pair, 0)) {
        Py_DECREF(key);
        Py_DECREF(value);
        return NULL;
    }

    PyTuple_SET_ITEM(pair, 0, key);
    PyTuple_SET_ITEM(pair, 1, value);
    return pair;
}


// the value of a cached key. a key the dict no longer holds means the cache
// moved under the reader, who retries. 0 ok, -1 exception set
static int cached_value(SortedDict *self, PyObject *data, PyObject *key, PyObject **value, bool *moved)
{
    int found = PyDict_GetItemRef(data, key, value);

    if (EXPECT(found == 0, 0)) {
        SortedDict_invalidate(self);
        *moved = true;
        PyErr_SetObject(PyExc_KeyError, key);
    }

    return found > 0 ? 0 : -1;
}


// append to the change log. a full log can only be described by a sort
static void log_append(SortedDict *self, uint8_t op, PyObject *key)
{
    if (self->pend_count == SD_PENDING_MAX) {
        SortedDict_invalidate(self);
        return;
    }

    self->pend[self->pend_count].key = Py_NewRef(key);
    self->pend[self->pend_count].op = op;
    self->pend_count++;
    self->version++;
}


static void SortedDict_dealloc(SortedDict *self)
{
    PyObject_GC_UnTrack(self);
    flush_pending(self);
    karr_install(self, NULL, 0);
    Py_CLEAR(self->data);
    Py_TYPE(self)->tp_free((PyObject *) self);
}


static int SortedDict_traverse(SortedDict *self, visitproc visit, void *arg)
{
    Py_VISIT(self->data);

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
    SortedDict_invalidate(self);
    karr_install(self, NULL, 0);

    // emptied rather than dropped, which breaks any cycle and keeps the methods usable
    if (self->data) {
        PyDict_Clear(self->data);
    }

    return 0;
}


PyObject *SortedDict_new(PyTypeObject *type, PyObject *args, PyObject *kwds)
{
    // tp_alloc zeroes the struct: ascending, no depth, nothing cached or pending
    SortedDict *self = (SortedDict *) type->tp_alloc(type, 0);

    if (self) {
        self->data = PyDict_New();
        if (!self->data) {
            Py_DECREF(self);
            return NULL;
        }
    }

    return (PyObject *) self;
}


// swap in a new data dict (reference stolen), dropping everything cached
int SortedDict_replace(SortedDict *self, PyObject *data)
{
    PyObject *previous = self->data;

    self->data = data;
    SortedDict_invalidate(self);
    karr_install(self, NULL, 0);
    // last - a finalizer can reenter
    Py_DECREF(previous);

    return self->truncate ? truncate_to_depth(self) : 0;
}


static int locked_init(SortedDict *self, PyObject *args, PyObject *kwds)
{
    static char *kwlist[] = {"data", "ordering", "max_depth", "truncate", NULL};
    PyObject *data = Py_None;
    const char *ordering = NULL;
    int depth = self->depth;
    int truncate = self->truncate;

    if (!PyArg_ParseTupleAndKeywords(args, kwds, "|O$sip", kwlist, &data, &ordering, &depth, &truncate)) {
        return -1;
    }

    if (data != Py_None && !PyDict_Check(data)) {
        PyErr_SetString(PyExc_TypeError, "data must be a dict");
        return -1;
    }

    if (depth < 0) {
        PyErr_SetString(PyExc_ValueError, "max_depth cannot be negative");
        return -1;
    }

    enum Ordering requested = self->ordering;
    if (ordering) {
        if (!strcmp(ordering, "ASC")) {
            requested = ASCENDING;
        } else if (!strcmp(ordering, "DESC")) {
            requested = DESCENDING;
        } else {
            PyErr_SetString(PyExc_ValueError, "ordering must be one of ASC or DESC");
            return -1;
        }
    }

    // everything validated, so commit
    self->depth = depth;
    self->truncate = truncate;

    if (data != Py_None) {
        PyObject *copy = PyDict_Copy(data);
        if (EXPECT(!copy, 0)) {
            return -1;
        }

        self->ordering = requested;
        return SortedDict_replace(self, copy);
    }

    if (requested != self->ordering) {
        self->ordering = requested;
        SortedDict_invalidate(self);
        karr_install(self, NULL, 0);
    }

    return self->truncate ? truncate_to_depth(self) : 0;
}


static int SortedDict_init(SortedDict *self, PyObject *args, PyObject *kwds)
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


// does the cached key at 'at' equal key. -1 exception
static int cached_eq(SortedDict *self, Py_ssize_t at, PyObject *key)
{
    PyObject *probe = Py_NewRef(self->karr[at]);
    int eq = PyObject_RichCompareBool(probe, key, Py_EQ);
    Py_DECREF(probe);
    return eq;
}


// put a list of keys in the cache's order
static int sort_keys(const SortedDict *self, PyObject *keys)
{
    if (EXPECT(PyList_Sort(keys) < 0, 0)) {
        return -1;
    }

    return (self->ordering == DESCENDING) ? PyList_Reverse(keys) : 0;
}


// rebuild the cache by sorting the dict's keys. ret 0 ok, 1 exception set
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

        if (EXPECT(sort_keys(self, keys) < 0, 0)) {
            Py_DECREF(keys);
            return 1;
        }

        Py_ssize_t n = PyList_GET_SIZE(keys);
        PyObject **arr = PyMem_New(PyObject *, n > 0 ? n : 1);
        if (EXPECT(!arr, 0)) {
            Py_DECREF(keys);
            PyErr_NoMemory();
            return 1;
        }

        // an empty list has no item array, and memcpy from NULL is undefined even for 0 bytes
        if (n > 0) {
            memcpy(arr, PySequence_Fast_ITEMS(keys), n * sizeof(PyObject *));
        }
        for (Py_ssize_t i = 0; i < n; ++i) {
            Py_INCREF(arr[i]);
        }
        Py_DECREF(keys);

        if (EXPECT(self->version == version, 1) || attempt == 3) {
            // after three retries a comparator is mutating the book on purpose. install anyway and stay dirty
            self->dirty = (self->version != version);
            karr_install(self, arr, n);
            return 0;
        }

        for (Py_ssize_t i = 0; i < n; ++i) {
            Py_DECREF(arr[i]);
        }
        PyMem_Free(arr);
    }
}


// split the log into keys to insert, keys to delete and keys to replace. a key
// logged more than once nets out by its first and last op: insert..insert is an
// insert, delete..delete a delete, insert..delete nothing, and delete..insert a
// replace. the price is still quoted but the dict now holds the newest object,
// and the cache must hand out that one. the latest logged entry is the one
// inserted. state packs (index of the latest entry << 2 | first op << 1 | odd count)
static int dedup_pending(const PendingEntry *pend, Py_ssize_t count, PyObject *inserts, PyObject *deletes, PyObject *replaces)
{
    PyObject *seen = PyDict_New();
    int ret = -1;

    if (EXPECT(!seen, 0)) {
        return -1;
    }

    for (Py_ssize_t i = 0; i < count; ++i) {
        PyObject *packed = PyDict_GetItemWithError(seen, pend[i].key);
        long state;

        if (packed) {
            state = (long) (i << 2) | ((PyLong_AsLong(packed) & 3) ^ 1);
        } else if (EXPECT(PyErr_Occurred() != NULL, 0)) {
            goto done;
        } else {
            state = (long) (i << 2) | (pend[i].op << 1) | 1;
        }

        packed = PyLong_FromLong(state);
        if (EXPECT(!packed, 0)) {
            goto done;
        }

        int failed = PyDict_SetItem(seen, pend[i].key, packed) < 0;
        Py_DECREF(packed);
        if (EXPECT(failed, 0)) {
            goto done;
        }
    }

    PyObject *key;
    PyObject *packed;
    Py_ssize_t pos = 0;

    while (PyDict_Next(seen, &pos, &key, &packed)) {
        long state = PyLong_AsLong(packed);
        int first = (state >> 1) & 1;
        int last = (state & 1) ? first : !first;
        PyObject *newest = pend[state >> 2].key;
        int failed = 0;

        if (first == PENDING_DELETE) {
            failed = (last == PENDING_DELETE) ? PyList_Append(deletes, key) : PyList_Append(replaces, newest);
        } else if (last == PENDING_INSERT) {
            failed = PyList_Append(inserts, newest);
        }

        if (EXPECT(failed < 0, 0)) {
            goto done;
        }
    }

    ret = 0;

done:
    Py_DECREF(seen);
    return ret;
}


// where the cached key equal to each entry of 'keys' sits. ret 0 ok, 1 the
// cache does not match the log (sort needed), -1 exception
static int locate_cached(SortedDict *self, uint64_t version, PyObject *keys, Py_ssize_t *pos)
{
    Py_ssize_t n = PyList_GET_SIZE(keys);

    for (Py_ssize_t i = 0; i < n; ++i) {
        Py_ssize_t at = keys_bisect(self, version, PyList_GET_ITEM(keys, i));
        if (EXPECT(at < 0, 0)) {
            return (at == -2) ? 1 : -1;
        }

        if (EXPECT(at == self->k_len, 0)) {
            return 1;
        }

        int eq = cached_eq(self, at, PyList_GET_ITEM(keys, i));
        if (EXPECT(eq < 0, 0)) {
            return -1;
        }

        if (EXPECT(self->version != version || !eq, 0)) {
            return 1;
        }

        pos[i] = at;
    }

    return 0;
}


static bool strictly_increasing(const Py_ssize_t *pos, Py_ssize_t n)
{
    for (Py_ssize_t i = 1; i < n; ++i) {
        if (pos[i] <= pos[i - 1]) {
            return false;
        }
    }

    return true;
}


// merge the pending log into the key cache
// ret 0  - key cache is up to date
// ret 1  - full sort needed
// ret -1 - exception
static int apply_pending(SortedDict *self)
{
    uint64_t version = self->version;
    PendingEntry pend[SD_PENDING_MAX];
    Py_ssize_t count = self->pend_count;
    Py_ssize_t ins_pos[SD_PENDING_MAX];
    Py_ssize_t del_pos[SD_PENDING_MAX];
    Py_ssize_t rep_pos[SD_PENDING_MAX];
    int status = -1;

    // take the log. the dict may be mutated by any comparison or hash below so
    // hold the ref. the key array needs no hold: every python call is followed
    // by a version check before the array is touched again
    memcpy(pend, self->pend, count * sizeof(PendingEntry));
    self->pend_count = 0;

    PyObject *data = Py_NewRef(self->data);
    PyObject *inserts = PyList_New(0);
    PyObject *deletes = PyList_New(0);
    PyObject *replaces = PyList_New(0);

    if (EXPECT(!inserts || !deletes || !replaces, 0)) {
        goto done;
    }

    if (count == 1) {
        if (EXPECT(PyList_Append(pend[0].op == PENDING_INSERT ? inserts : deletes, pend[0].key) < 0, 0)) {
            goto done;
        }
    } else if (EXPECT(dedup_pending(pend, count, inserts, deletes, replaces), 0)) {
        goto done;
    }

    Py_ssize_t num_ins = PyList_GET_SIZE(inserts);
    Py_ssize_t num_del = PyList_GET_SIZE(deletes);
    Py_ssize_t num_rep = PyList_GET_SIZE(replaces);

    if (num_ins == 0 && num_del == 0 && num_rep == 0) {
        status = (self->version == version && self->pend_count == 0 && !self->dirty) ? 0 : 1;
        goto done;
    }

    // inserts and replaces are merged in key order. the deletes came out of a
    // dict in no order, so their positions are sorted once found
    if (EXPECT(sort_keys(self, inserts) < 0, 0) || EXPECT(sort_keys(self, replaces) < 0, 0)) {
        goto done;
    }

    if (EXPECT(self->version != version, 0)) {
        status = 1;
        goto done;
    }

    Py_ssize_t old_size = self->k_len;

    // every delete and every replace must be cached
    status = locate_cached(self, version, deletes, del_pos);
    if (status == 0) {
        status = locate_cached(self, version, replaces, rep_pos);
    }
    if (status != 0) {
        goto done;
    }
    status = -1;

    for (Py_ssize_t i = 1; i < num_del; ++i) {
        Py_ssize_t value = del_pos[i];
        Py_ssize_t j = i;
        while (j > 0 && del_pos[j - 1] > value) {
            del_pos[j] = del_pos[j - 1];
            j--;
        }
        del_pos[j] = value;
    }

    if (EXPECT(!strictly_increasing(del_pos, num_del) || !strictly_increasing(rep_pos, num_rep), 0)) {
        status = 1;
        goto done;
    }

    // no insert may already be cached
    for (Py_ssize_t i = 0; i < num_ins; ++i) {
        Py_ssize_t at = keys_bisect(self, version, PyList_GET_ITEM(inserts, i));
        if (EXPECT(at < 0, 0)) {
            status = (at == -2) ? 1 : -1;
            goto done;
        }

        if (at < old_size) {
            int eq = cached_eq(self, at, PyList_GET_ITEM(inserts, i));
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

    for (Py_ssize_t i = 1; i < num_ins; ++i) {
        if (EXPECT(ins_pos[i] < ins_pos[i - 1], 0)) {
            status = 1;
            goto done;
        }
    }

    // nothing python runs from here to the publish, so the array can be rebuilt.
    // the keys going out are released only after it is consistent
    PyObject *dropped[SD_PENDING_MAX];
    Py_ssize_t num_dropped = 0;

    if (num_ins == 0 && num_del == 0) {
        // swap the pointers in place
        for (Py_ssize_t i = 0; i < num_rep; ++i) {
            dropped[num_dropped++] = self->karr[rep_pos[i]];
            self->karr[rep_pos[i]] = Py_NewRef(PyList_GET_ITEM(replaces, i));
        }
    } else {
        // merge by moving surviving pointers into a fresh array. survivors
        // transfer their reference, so there is no refcount traffic and
        // no O(N) tuple to build and destroy
        PyObject **scratch = PyMem_New(PyObject *, new_size > 0 ? new_size : 1);
        if (EXPECT(!scratch, 0)) {
            PyErr_NoMemory();
            goto done;
        }

        Py_ssize_t ri = 0;
        Py_ssize_t ii = 0;
        Py_ssize_t di = 0;
        Py_ssize_t pi = 0;
        Py_ssize_t oi = 0;

        // copy the untouched runs between change positions with memcpy. at a
        // position an insert lands first, then a replacement takes the old key's place
        while (1) {
            Py_ssize_t next_ins = (ii < num_ins) ? ins_pos[ii] : old_size;
            Py_ssize_t next_del = (di < num_del) ? del_pos[di] : old_size;
            Py_ssize_t next_rep = (pi < num_rep) ? rep_pos[pi] : old_size;
            Py_ssize_t stop = (next_ins < next_del) ? next_ins : next_del;
            if (next_rep < stop) {
                stop = next_rep;
            }

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

            if (pi < num_rep && rep_pos[pi] == oi) {
                scratch[ri++] = Py_NewRef(PyList_GET_ITEM(replaces, pi));
                dropped[num_dropped++] = self->karr[oi];
                pi++;
                oi++;
                continue;
            }

            if (di < num_del && del_pos[di] == oi) {
                dropped[num_dropped++] = self->karr[oi];
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
            for (Py_ssize_t i = 0; i < pi; ++i) {
                Py_DECREF(PyList_GET_ITEM(replaces, i));
            }

            PyMem_Free(scratch);
            status = 1;
            goto done;
        }

        PyObject **prev = self->karr;
        self->karr = scratch;
        self->k_len = new_size;
        PyMem_Free(prev);
    }

    self->version++;
    for (Py_ssize_t i = 0; i < num_dropped; ++i) {
        Py_DECREF(dropped[i]);
    }
    status = 0;

done:
    if (status != 0) {
        SortedDict_invalidate(self);
    }

    Py_XDECREF(inserts);
    Py_XDECREF(deletes);
    Py_XDECREF(replaces);
    Py_DECREF(data);

    for (Py_ssize_t i = 0; i < count; ++i) {
        Py_DECREF(pend[i].key);
    }

    return status;
}


// bring the key cache up to date. ret 0 ok, 1 exception set
int update_keys(SortedDict *self)
{
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

        return full_sort(self);
    }
}


static PyObject *build_items_once(SortedDict *self, bool *moved)
{
    *moved = false;

    if (EXPECT(update_keys(self), 0)) {
        return NULL;
    }

    // a key hash can reenter and move the cache or the dict, so hold both
    PyObject *keys = SortedDict_key_window(self, visible_len(self));
    if (EXPECT(!keys, 0)) {
        return NULL;
    }

    PyObject *data = Py_NewRef(self->data);
    Py_ssize_t len = PyTuple_GET_SIZE(keys);
    PyObject *ret = PyList_New(len);

    for (Py_ssize_t i = 0; ret && i < len; ++i) {
        PyObject *key = PyTuple_GET_ITEM(keys, i);
        PyObject *value;

        if (EXPECT(cached_value(self, data, key, &value, moved), 0)) {
            Py_CLEAR(ret);
            break;
        }

        PyObject *entry = make_pair(Py_NewRef(key), value);
        if (EXPECT(!entry, 0)) {
            Py_CLEAR(ret);
            break;
        }

        PyList_SET_ITEM(ret, i, entry);
    }

    Py_DECREF(keys);
    Py_DECREF(data);
    return ret;
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


static PyObject *SortedDict_keys(SortedDict *self, PyObject *Py_UNUSED(ignored))
{
    PyObject *ret = NULL;

    Py_BEGIN_CRITICAL_SECTION(self);
    if (EXPECT(!update_keys(self), 1)) {
        ret = SortedDict_key_window(self, visible_len(self));
    }
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

        // ties go to the candidate: a later insert of an equal key is the object the dict holds
        int keep = PyObject_RichCompareBool(best, cand, self->ordering == DESCENDING ? Py_GT : Py_LT);
        if (EXPECT(keep < 0, 0)) {
            Py_DECREF(cand);
            status = -1;
            goto done;
        }

        if (EXPECT(self->version != version, 0)) {
            Py_DECREF(cand);
            goto done;
        }

        if (keep) {
            Py_DECREF(cand);
        } else {
            Py_SETREF(best, cand);
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

    *out = make_pair(Py_NewRef(best), value);
    status = *out ? 0 : -1;

done:
    Py_XDECREF(best);
    Py_DECREF(data);
    return status;
}


static PyObject *index_once(SortedDict *self, Py_ssize_t i, bool *moved)
{
    *moved = false;

    if (EXPECT(update_keys(self), 0)) {
        return NULL;
    }

    Py_ssize_t len = visible_len(self);
    if (i < 0) {
        i += len;
    }

    if (EXPECT(i < 0 || i >= len, 0)) {
        PyErr_SetString(PyExc_IndexError, "index out of range");
        return NULL;
    }

    PyObject *key = Py_NewRef(self->karr[i]);
    PyObject *data = Py_NewRef(self->data);
    PyObject *value;
    int ret = cached_value(self, data, key, &value, moved);
    Py_DECREF(data);

    if (EXPECT(ret, 0)) {
        Py_DECREF(key);
        return NULL;
    }

    return make_pair(key, value);
}


static PyObject *locked_index(SortedDict *self, Py_ssize_t i)
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


static PyObject *SortedDict_index(SortedDict *self, PyObject *index)
{
    Py_ssize_t i = PyLong_AsSsize_t(index);
    if (EXPECT(i == -1 && PyErr_Occurred(), 0)) {
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

    // a key hash or a conversion callback may reenter and mutate the book, so hold both
    PyObject *keys = SortedDict_key_window(self, visible_len(self));
    if (EXPECT(!keys, 0)) {
        return NULL;
    }

    PyObject *data = Py_NewRef(self->data);
    PyObject *values = NULL;
    PyObject *ret = NULL;
    Py_ssize_t len = PyTuple_GET_SIZE(keys);

    if (to) {
        // conversion runs arbitrary python, so snapshot the values before converting anything
        values = PyList_New(len);
        if (EXPECT(!values, 0)) {
            goto done;
        }

        for (Py_ssize_t i = 0; i < len; ++i) {
            PyObject *value;
            if (EXPECT(cached_value(self, data, PyTuple_GET_ITEM(keys, i), &value, moved), 0)) {
                goto done;
            }
            PyList_SET_ITEM(values, i, value);
        }
    }

    ret = PyDict_New();
    if (EXPECT(!ret, 0)) {
        goto done;
    }

    for (Py_ssize_t i = 0; i < len; ++i) {
        // borrowed from the held tuple, which outlives every call below
        PyObject *key = PyTuple_GET_ITEM(keys, i);
        PyObject *value;
        int failed;

        if (to) {
            Py_INCREF(key);
            value = Py_NewRef(PyList_GET_ITEM(values, i));
            failed = convert_item(&key, from, to) || convert_item(&value, from, to) || PyDict_SetItem(ret, key, value) < 0;
            Py_DECREF(key);
        } else {
            if (EXPECT(cached_value(self, data, key, &value, moved), 0)) {
                Py_CLEAR(ret);
                goto done;
            }
            // the insert hashes the key, which can reenter
            failed = PyDict_SetItem(ret, key, value) < 0;
        }

        Py_DECREF(value);

        if (EXPECT(failed, 0)) {
            Py_CLEAR(ret);
            goto done;
        }
    }

done:
    Py_XDECREF(values);
    Py_DECREF(keys);
    Py_DECREF(data);
    return ret;
}


PyObject *locked_SortedDict_todict(SortedDict *self, PyObject *from, PyObject *to)
{
    bool moved;
    PyObject *ret = todict_once(self, from, to, &moved);

    for (int attempt = 0; !ret && moved && attempt < SD_READ_RETRIES; ++attempt) {
        PyErr_Clear();
        ret = todict_once(self, from, to, &moved);
    }

    return ret;
}


static PyObject *SortedDict_todict(SortedDict *self, PyObject *args, PyObject *kwargs)
{
    static char *kwlist[] = {"from_type", "to_type", NULL};
    PyObject *from = NULL;
    PyObject *to = NULL;

    if (!PyArg_ParseTupleAndKeywords(args, kwargs, "|$OO", kwlist, &from, &to)) {
        return NULL;
    }

    PyObject *ret;
    Py_BEGIN_CRITICAL_SECTION(self);
    ret = locked_SortedDict_todict(self, from, to);
    Py_END_CRITICAL_SECTION();
    return ret;
}


static PyObject *SortedDict_tolist(SortedDict *self, PyObject *Py_UNUSED(ignored))
{
    PyObject *ret;
    Py_BEGIN_CRITICAL_SECTION(self);
    ret = build_items(self);
    Py_END_CRITICAL_SECTION();
    return ret;
}


// drop the levels past max_depth, evicting from the tail of the cache one key at a time
int truncate_to_depth(SortedDict *self)
{
    if (self->depth <= 0) {
        return 0;
    }

    while (true) {
        if (EXPECT(update_keys(self), 0)) {
            return -1;
        }

        if (self->k_len <= self->depth) {
            return 0;
        }

        uint64_t version = self->version;
        PyObject *key = Py_NewRef(self->karr[self->k_len - 1]);
        PyObject *data = Py_NewRef(self->data);
        int ret = PyDict_DelItem(data, key);
        Py_DECREF(data);

        if (EXPECT(ret < 0, 0)) {
            if (!PyErr_ExceptionMatches(PyExc_KeyError)) {
                Py_DECREF(key);
                SortedDict_invalidate(self);
                return -1;
            }
            // already gone: a reentrant delete got there first
            PyErr_Clear();
        }

        if (EXPECT(self->version != version, 0)) {
            // the cache moved under the delete, so recompute it
            Py_DECREF(key);
            SortedDict_invalidate(self);
            continue;
        }

        // shrink in place, then release the cache's reference and ours
        self->k_len--;
        self->version++;
        Py_DECREF(key);
        Py_DECREF(key);
    }
}


static PyObject *SortedDict_truncate(SortedDict *self, PyObject *Py_UNUSED(ignored))
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
    return (self->depth > 0 && self->depth < len) ? self->depth : len;
}


static Py_ssize_t SortedDict_len(SortedDict *self)
{
    Py_ssize_t ret;
    Py_BEGIN_CRITICAL_SECTION(self);
    ret = locked_SortedDict_len(self);
    Py_END_CRITICAL_SECTION();
    return ret;
}


static PyObject *SortedDict_get_data(SortedDict *self, void *Py_UNUSED(closure))
{
    PyObject *ret;
    Py_BEGIN_CRITICAL_SECTION(self);
    ret = Py_NewRef(self->data);
    Py_END_CRITICAL_SECTION();
    return ret;
}


static PyObject *SortedDict_getitem(SortedDict *self, PyObject *key)
{
    PyObject *value;
    int found;

    Py_BEGIN_CRITICAL_SECTION(self);
    PyObject *data = Py_NewRef(self->data);
    found = PyDict_GetItemRef(data, key, &value);
    Py_DECREF(data);
    Py_END_CRITICAL_SECTION();

    if (EXPECT(found == 0, 0)) {
        PyErr_SetObject(PyExc_KeyError, key);
    }

    return value;
}


static int locked_setitem(SortedDict *self, PyObject *key, PyObject *value)
{
    bool cache_live = (!self->dirty && self->karr != NULL);
    uint64_t version = self->version;

    // a full log is merged now rather than escalated, so a burst of writes stays incremental
    if (EXPECT(cache_live && self->pend_count == SD_PENDING_MAX, 0)) {
        if (apply_pending(self) != 0 && PyErr_Occurred()) {
            // the write can still go ahead; whatever went wrong resurfaces on the next read
            PyErr_Clear();
        }
        cache_live = (!self->dirty && self->karr != NULL);
        version = self->version;
    }

    PyObject *data = Py_NewRef(self->data);

    if (!value) {
        // a delete. bump first, so a reader that gets in while the entry is going sees the change
        uint64_t claimed = ++self->version;
        int ret = PyDict_DelItem(data, key);
        Py_DECREF(data);

        if (ret == 0) {
            if (cache_live && self->version == claimed) {
                log_append(self, PENDING_DELETE, key);
            } else {
                SortedDict_invalidate(self);
            }
        }

        // a failed delete leaves the dict, the cache and the log as they were
        return ret;
    }

    Py_ssize_t before = PyDict_GET_SIZE(data);
    int ret = PyDict_SetItem(data, key, value);
    Py_ssize_t after = PyDict_GET_SIZE(data);
    Py_DECREF(data);

    if (EXPECT(ret < 0, 0)) {
        return ret;
    }

    if (after == before && self->version == version) {
        // in place value update, the key set did not change
        return 0;
    }

    if (!cache_live) {
        self->dirty = true;
        self->version++;
    } else if (after == before + 1 && self->version == version) {
        log_append(self, PENDING_INSERT, key);
    } else {
        SortedDict_invalidate(self);
    }

    return (self->truncate && truncate_to_depth(self)) ? -1 : 0;
}


static int SortedDict_setitem(SortedDict *self, PyObject *key, PyObject *value)
{
    int ret;
    Py_BEGIN_CRITICAL_SECTION(self);
    ret = locked_setitem(self, key, value);
    Py_END_CRITICAL_SECTION();
    return ret;
}


static int SortedDict_contains(SortedDict *self, PyObject *value)
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
typedef struct {
    PyObject_HEAD
    PyObject *keys;    // immutable window of the cache, so the walk survives any mutation
    PyObject *data;
    PyObject *owner;
    Py_ssize_t index;
    bool pairs;
} SortedDictIter;


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
    Py_ssize_t len = PyTuple_GET_SIZE(self->keys);
#ifdef Py_GIL_DISABLED
    Py_ssize_t index = _Py_atomic_add_ssize(&self->index, 1);

    if (index >= len) {
        _Py_atomic_add_ssize(&self->index, -1);
        return NULL;
    }
#else
    Py_ssize_t index = self->index;

    if (index >= len) {
        return NULL;
    }

    self->index = index + 1;
#endif

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

    return make_pair(Py_NewRef(key), value);
}


PyTypeObject SortedDictIterType = {
    PyVarObject_HEAD_INIT(NULL, 0)
    .tp_name = "order_book.side_iterator",
    .tp_doc = "iterator over the keys or (key, value) pairs of a SortedDict",
    .tp_basicsize = sizeof(SortedDictIter),
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

    PyObject *keys = SortedDict_key_window(self, visible_len(self));
    if (EXPECT(!keys, 0)) {
        return NULL;
    }

    SortedDictIter *it = PyObject_GC_New(SortedDictIter, &SortedDictIterType);
    if (EXPECT(!it, 0)) {
        Py_DECREF(keys);
        return NULL;
    }

    it->keys = keys;
    it->data = Py_NewRef(self->data);
    it->owner = Py_NewRef((PyObject *) self);
    it->index = 0;
    it->pairs = pairs;

    PyObject_GC_Track(it);
    return (PyObject *) it;
}


static PyObject *SortedDict_iter_new(SortedDict *self, bool pairs)
{
    PyObject *ret;
    Py_BEGIN_CRITICAL_SECTION(self);
    ret = locked_iter_new(self, pairs);
    Py_END_CRITICAL_SECTION();
    return ret;
}


static PyObject *SortedDict_getiter(SortedDict *self)
{
    return SortedDict_iter_new(self, false);
}


static PyObject *SortedDict_items(SortedDict *self, PyObject *Py_UNUSED(ignored))
{
    return SortedDict_iter_new(self, true);
}


// the configuration fields are getters rather than PyMemberDef entries: a member
// descriptor reads the field without taking the critical section (a relaxed atomic
// load on free-threaded builds), which is a data race against the plain stores
// locked_init makes under the lock
static PyObject *SortedDict_get_ordering(SortedDict *self, void *Py_UNUSED(closure))
{
    int ret;
    Py_BEGIN_CRITICAL_SECTION(self);
    ret = self->ordering;
    Py_END_CRITICAL_SECTION();
    return PyLong_FromLong(ret);
}


static PyObject *SortedDict_get_truncate(SortedDict *self, void *Py_UNUSED(closure))
{
    bool ret;
    Py_BEGIN_CRITICAL_SECTION(self);
    ret = self->truncate;
    Py_END_CRITICAL_SECTION();
    return PyBool_FromLong(ret);
}


static PyObject *SortedDict_get_max_depth(SortedDict *self, void *Py_UNUSED(closure))
{
    int ret;
    Py_BEGIN_CRITICAL_SECTION(self);
    ret = self->depth;
    Py_END_CRITICAL_SECTION();
    return PyLong_FromLong(ret);
}


/* type definition */
static PyGetSetDef SortedDict_getset[] = {
    {"__data", (getter) SortedDict_get_data, NULL, "internal data", NULL},
    {"__ordering", (getter) SortedDict_get_ordering, NULL, "ordering flag", NULL},
    {"__truncate", (getter) SortedDict_get_truncate, NULL, "truncate flag", NULL},
    {"__max_depth", (getter) SortedDict_get_max_depth, NULL, "maximum depth", NULL},
    {NULL}
};

static PyMethodDef SortedDict_methods[] = {
    {"keys", (PyCFunction) SortedDict_keys, METH_NOARGS, "return a tuple of keys in sorted order"},
    {"index", (PyCFunction) SortedDict_index, METH_O, "return a key, value tuple at index N"},
    {"truncate", (PyCFunction) SortedDict_truncate, METH_NOARGS, "truncate to length max_depth"},
    {"to_dict", (PyCFunction) SortedDict_todict, METH_VARARGS | METH_KEYWORDS, "return a python dictionary, sorted by keys"},
    {"to_list", (PyCFunction) SortedDict_tolist, METH_NOARGS, "return a list of key, value tuples."},
    {"items", (PyCFunction) SortedDict_items, METH_NOARGS, "return an iterator over (key, value) pairs, sorted by key"},
    {NULL}
};

static PyMappingMethods SortedDict_mapping = {
    (lenfunc) SortedDict_len,
    (binaryfunc) SortedDict_getitem,
    (objobjargproc) SortedDict_setitem
};

static PySequenceMethods SortedDict_seq = {
    .sq_contains = (objobjproc) SortedDict_contains
};

PyTypeObject SortedDictType = {
    PyVarObject_HEAD_INIT(NULL, 0)
    .tp_name = "order_book.SortedDict",
    .tp_doc = "A SortedDict data structure",
    .tp_basicsize = sizeof(SortedDict),
    .tp_flags = Py_TPFLAGS_DEFAULT | Py_TPFLAGS_BASETYPE | Py_TPFLAGS_HAVE_GC,
    .tp_new = SortedDict_new,
    .tp_init = (initproc) SortedDict_init,
    .tp_dealloc = (destructor) SortedDict_dealloc,
    .tp_traverse = (traverseproc) SortedDict_traverse,
    .tp_clear = (inquiry) SortedDict_clear,
    .tp_getset = SortedDict_getset,
    .tp_methods = SortedDict_methods,
    .tp_as_mapping = &SortedDict_mapping,
    .tp_as_sequence = &SortedDict_seq,
    .tp_iter = (getiterfunc) SortedDict_getiter,
};
