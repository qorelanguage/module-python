/* indent-tabs-mode: nil -*- */
/*
    qore Python module

    Copyright (C) 2020 Qore Technologies, s.r.o.

    This library is free software; you can redistribute it and/or
    modify it under the terms of the GNU Lesser General Public
    License as published by the Free Software Foundation; either
    version 2.1 of the License, or (at your option) any later version.

    This library is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
    Lesser General Public License for more details.

    You should have received a copy of the GNU Lesser General Public
    License along with this library; if not, write to the Free Software
    Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
*/

#include "python-module.h"

class QoreMetaPathFinder {
public:
    //PyObject_HEAD

    static void dealloc(PyObject* self) {
        //PyObject_GC_UnTrack(self);
        Py_TYPE(self)->tp_free(self);
    }

    static PyObject* repr(PyObject* obj) {
        QoreStringMaker str("QoreMetaPathFinder object %p", obj);
        return PyUnicode_FromKindAndData(PyUnicode_1BYTE_KIND, str.c_str(), str.size());
    }
};

PyDoc_STRVAR(QoreMetaPathFinder_doc,
"QoreMetaPathFinder()\n\
\n\
Creates Python wrappers for Qore code.");

static PyMethodDef QoreMetaPathFinder_methods[] = {
    {NULL, NULL}
};

PyTypeObject QoreMetaPathFinder_Type = {
    PyVarObject_HEAD_INIT(nullptr, 0)
    "QoreMetaPathFinder",         // tp_name
    sizeof(QoreMetaPathFinder),   // tp_basicsize
    0,                            // tp_itemsize
    // Slots
    (destructor)QoreMetaPathFinder::dealloc,  // tp_dealloc
    0,                            // tp_vectorcall_offset
    0,                            // tp_getattr
    0,                            // tp_setattr
    0,                            // tp_as_async
    QoreMetaPathFinder::repr,     // tp_repr
    0,                            // tp_as_number
    0,                            // tp_as_sequence
    0,                            // tp_as_mapping
    0,                            // tp_hash*/
    0,                            // tp_call*/
    0,                            // tp_str
    PyObject_GenericGetAttr,      // tp_getattro
    0,                            // tp_setattro
    0,                            // tp_as_buffer
    Py_TPFLAGS_DEFAULT,           // tp_flags
    QoreMetaPathFinder_doc,       // tp_doc
    0,                            // tp_traverse
    0,                            // tp_clear
    0,                            // tp_richcompare
    0,                            // tp_weaklistoffset
    0,                            // tp_iter
    0,                            // tp_iternext
    QoreMetaPathFinder_methods,   // tp_methods
    0,                            // tp_members
    0,                            // tp_getset
    &PyBaseObject_Type,           // tp_base
    0,                            // tp_dict
    0,                            // tp_descr_get
    0,                            // tp_descr_set
    0,                            // tp_dictoffset
    0,                            // tp_init
    0,                            // tp_alloc
    0,                            // tp_new
    PyObject_Del,                 // tp_free
};
