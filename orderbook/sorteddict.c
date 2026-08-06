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
void SortedDict_dealloc(SortedDict *self)
{
    PyObject_GC_UnTrack(self);
    Py_CLEAR(self->keys);
    Py_CLEAR(self->data);
    Py_TYPE(self)->tp_free((PyObject *) self);
}


int SortedDict_traverse(SortedDict *self, visitproc visit, void *arg)
{
    Py_VISIT(self->data);
    Py_VISIT(self->keys);
    return 0;
}


int SortedDict_clear(SortedDict *self)
{
    Py_CLEAR(self->keys);

    // the two sides are emptied rather than dropped which is enough
    // to break any cycle
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
        // the cached keys describe the old data, they cannot be reused
        Py_CLEAR(self->keys);
        self->dirty = true;
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

            PyObject *str = PyUnicode_AsEncodedString(ordering_arg, "UTF-8", "strict");
            if (!str) {
                return -1;
            }

            const char *value = PyBytes_AsString(str);

            if (value) {
                if (strcmp(value, "DESC") == 0) {
                    self->ordering = DESCENDING;
                } else if (strcmp(value, "ASC") == 0) {
                    self->ordering = ASCENDING;
                } else {
                    Py_DECREF(str);
                    PyErr_SetString(PyExc_ValueError, "ordering must be one of ASC or DESC");
                    return -1;
                }
            }
            Py_DECREF(str);
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


/* internal helper function to update keys */
inline int update_keys(SortedDict *self) {
    if (!self->dirty && self->keys) {
       return 0;
    }

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

    Py_XSETREF(self->keys, ret);
    self->dirty = false;

    return 0;
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

    // new reference
    PyObject *key = PySequence_GetItem(self->keys, i);
    if (EXPECT(!key, 0)) {
        return NULL;
    }

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
    if (self->depth) {
        if (EXPECT(update_keys(self), 0)) {
            return -1;
        }

        PyObject *delete = PySequence_GetSlice(self->keys, self->depth, PyDict_Size(self->data));
        if (EXPECT(!delete, 0)) {
            return -1;
        }

        Py_ssize_t len = PySequence_Length(delete);
        if (EXPECT(len == -1, 0)) {
            Py_DECREF(delete);
            return -1;
        }

        for (Py_ssize_t i = 0; i < len; ++i) {
            if (EXPECT(PyDict_DelItem(self->data, PySequence_Fast_GET_ITEM(delete, i)) == -1, 0)) {
                Py_DECREF(delete);
                return -1;
            }
        }
        Py_DECREF(delete);

        if (len > 0) {
            self->dirty = true;
        }

        if (EXPECT(update_keys(self), 0)) {
            return -1;
        }
    }

    return 0;
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
    if (value) {
	if (EXPECT(PyDict_Contains(self->data, key) == 0, 0)) {
            self->dirty = true;
	}

        int ret = PyDict_SetItem(self->data, key, value);

        if (EXPECT(ret == -1, 0)) {
            return ret;
        } else if (EXPECT(self->truncate && truncate_to_depth(self), 0)) {
            return -1;
        }

        return ret;
    } else {
        self->dirty = true;
        // setitem also called to for del (value will be null for deletes)
        return PyDict_DelItem(self->data, key);
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
