/*
Copyright (C) 20202021  Bryant Moscon - bmoscon@gmail.com

Please see the LICENSE file for the terms and conditions
associated with this software.
*/
#include "sorteddict.h"
#include "utils.h"


/* Sorted Dictionary */
void SortedDict_dealloc(SortedDict *self)
{
    if (self->keys) {
        Py_DECREF(self->keys);
    }

    if (self->data) {
        Py_DECREF(self->data);
    }

    Py_TYPE(self)->tp_free((PyObject *) self);
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
        // 0 means it hasnt been set
        self->ordering = INVALID_ORDERING;
        // -1 means uninitalized
        self->iterator_index = -1;
        self->keys = NULL;
        self->dirty = false;
    }
    return (PyObject *) self;
}


int SortedDict_init(SortedDict *self, PyObject *args, PyObject *kwds)
{
    PyObject *ordering = NULL;
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
        if (self->data) {
            Py_DECREF(self->data);
        }
        self->data = copy;
    }


    if (kwds && PyDict_Check(kwds) && PyDict_Size(kwds) > 0) {
        ordering = PyDict_GetItemString(kwds, "ordering");
        if (!PyUnicode_Check(ordering)) {
            PyErr_SetString(PyExc_ValueError, "ordering must be a string");
            return -1;
        }

        PyObject *str = PyUnicode_AsEncodedString(ordering, "UTF-8", "strict");
        if (!str) {
            return -1;
        }

        char *value = PyBytes_AsString(str);

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

    return 0;
}


PyObject* SortedDict_keys(SortedDict *self, PyObject *Py_UNUSED(ignored))
{
    if (!self->dirty && self->keys) {
        Py_INCREF(self->keys);
        return self->keys;
    }

    PyObject *keys = PyDict_Keys(self->data);
    if (!keys) {
        return NULL;
    }

    if (PyList_Sort(keys) < 0) {
        return NULL;
    }

    if (self->ordering == DESCENDING) {
        if (PyList_Reverse(keys) < 0) {
            return NULL;
        }
    }

     PyObject *ret = PySequence_Tuple(keys);
     Py_DECREF(keys);
     if (!ret) {
         return NULL;
     }

    if (self->keys) {
        Py_DECREF(self->keys);
    }

    Py_INCREF(ret);
    self->keys = ret;
    self->dirty = false;
    return ret;
}


PyObject* SortedDict_index(SortedDict *self, PyObject *index)
{
    long i = PyLong_AsLong(index);
    if (PyErr_Occurred()) {
        return NULL;
    }

    PyObject *k = SortedDict_keys(self, NULL);
    if (!k) {
        return NULL;
    }
    Py_DECREF(k);

    // new reference
    PyObject *key = PySequence_GetItem(self->keys, i);
    if (!key) {
        return NULL;
    }

    // borrowed reference
    PyObject *value = PyDict_GetItem(self->data, key);
    if (!value) {
        Py_DECREF(key);
        return value;
    }

    PyObject *ret = PyTuple_New(2);
    if (!ret) {
        Py_DECREF(key);
        return NULL;
    }

    PyTuple_SetItem(ret, 0, key);
    Py_INCREF(value);
    PyTuple_SetItem(ret, 1, value);

    return ret;
}

PyObject* SortedDict_todict(SortedDict *self, PyObject *Py_UNUSED(ignored))
{
    PyObject *ret = PyDict_New();
    if (!ret) {
        return NULL;
    }

    PyObject *k = SortedDict_keys(self, NULL);
    if (!k) {
        return NULL;
    }
    Py_DECREF(k);

    int len = PySequence_Length(self->keys);
    for(int i = 0; i < len; ++i) {
        PyObject *key = PyTuple_GET_ITEM(self->keys, i);
        PyObject *value = PyDict_GetItem(self->data, key);
        PyDict_SetItem(ret, key, value);
    }

    return ret;
}


/* Sorted Dictionary Mapping Functions */
Py_ssize_t SortedDict_len(SortedDict *self)
{
	return PyDict_Size(self->data);
}

PyObject *SortedDict_getitem(SortedDict *self, PyObject *key)
{
    PyObject *ret = PyDict_GetItemWithError(self->data, key);
    if (ret) {
        Py_INCREF(ret);
        return ret;
    }

    if (!PyErr_Occurred()) {
        PyErr_SetString(PyExc_KeyError, "key does not exist");
    }

    return ret;
}

int SortedDict_setitem(SortedDict *self, PyObject *key, PyObject *value)
{
    self->dirty = true;

    if (value) {
        return PyDict_SetItem(self->data, key, value);
    } else {
        // setitem also called to for del (value will be null for deletes)
        return PyDict_DelItem(self->data, key);
    }
}

/* iterator methods */
PyObject *SortedDict_next(SortedDict *self)
{
    if (self->iterator_index == -1) {
        self->iterator_index = 0;

        PyObject *k = SortedDict_keys(self, NULL);
        if (!k) {
            return NULL;
        }
        Py_DECREF(k);

        Py_ssize_t size = PySequence_Fast_GET_SIZE(self->keys);
        if (size == 0){
            return NULL;
        }

        PyObject *ret = PySequence_Fast_GET_ITEM(self->keys, self->iterator_index);
        Py_INCREF(ret);
        return ret;
    } else {
        self->iterator_index++;
        Py_ssize_t size = PySequence_Fast_GET_SIZE(self->keys);
        if (size == self->iterator_index) {
            self->iterator_index = -1;
            return NULL;
        }
        PyObject *ret = PySequence_Fast_GET_ITEM(self->keys, self->iterator_index);
        Py_INCREF(ret);
        return ret;
    }
}
