#define PY_SSIZE_T_CLEAN
#include <stdint.h>
#include <Python.h>

#include "structmember.h"


typedef struct {
    PyObject_HEAD
    PyObject *data;
} SortedDict;


static void SortedDict_dealloc(SortedDict *self)
{
    Py_XDECREF(self->data);
    Py_TYPE(self)->tp_free((PyObject *) self);
}


static PyObject *SortedDict_new(PyTypeObject *type, PyObject *args, PyObject *kwds)
{
    SortedDict *self;
    self = (SortedDict *) type->tp_alloc(type, 0);
    if (self != NULL) {
        self->data = PyDict_New();
        if (!self->data) {
            Py_DECREF(self);
            return NULL;
        }
    }
    return (PyObject *) self;
}


static int SortedDict_init(SortedDict *self, PyObject *args, PyObject *kwds)
{
    return 0;
}


static PyMemberDef SortedDict_members[] = {
    {"__data", T_OBJECT_EX, offsetof(SortedDict, data), READONLY, "internal data"},
    {NULL}
};


static PyObject* SortedDict_placeholder(SortedDict *self, PyObject *Py_UNUSED(ignored))
{
    return PyUnicode_FromString("Hello!");
}


static PyMethodDef SortedDict_methods[] = {
    {"placeholder", (PyCFunction) SortedDict_placeholder, METH_NOARGS,
     "Placeholder"
    },
    {NULL}
};


static PyTypeObject SortedDictType = {
    PyVarObject_HEAD_INIT(NULL, 0)
    .tp_name = "sorteddict.sorteddict",
    .tp_doc = "An SortedDict data structure",
    .tp_basicsize = sizeof(SortedDict),
    .tp_itemsize = 0,
    .tp_flags = Py_TPFLAGS_DEFAULT | Py_TPFLAGS_BASETYPE,
    .tp_new = SortedDict_new,
    .tp_init = (initproc) SortedDict_init,
    .tp_dealloc = (destructor) SortedDict_dealloc,
    .tp_members = SortedDict_members,
    .tp_methods = SortedDict_methods,
};


static PyModuleDef sorteddictmodule = {
    PyModuleDef_HEAD_INIT,
    .m_name = "SortedDict",
    .m_doc = "Sorted Dictictionary data structure",
    .m_size = -1,
};


PyMODINIT_FUNC PyInit_sorteddict(void)
{
    PyObject *m;
    if (PyType_Ready(&SortedDictType) < 0)
        return NULL;

    m = PyModule_Create(&sorteddictmodule);
    if (m == NULL)
        return NULL;

    Py_INCREF(&SortedDictType);
    if (PyModule_AddObject(m, "SortedDict", (PyObject *) &SortedDictType) < 0) {
        Py_DECREF(&SortedDictType);
        Py_DECREF(m);
        return NULL;
    }

    return m;
}