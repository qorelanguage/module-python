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

#include "PythonQoreClass.h"
#include "QoreLoader.h"

#include <string.h>
#include <memory>

PyTypeObject PythonQoreClass::static_py_type = {
    PyVarObject_HEAD_INIT(nullptr, 0)
    .tp_basicsize = sizeof(PyQoreObject),
    .tp_dealloc = (destructor)PythonQoreClass::py_dealloc,
    .tp_repr = PythonQoreClass::py_repr,
    .tp_getattro = PyObject_GenericGetAttr,
    .tp_flags = Py_TPFLAGS_DEFAULT,
    .tp_base = &PyBaseObject_Type,
    .tp_alloc = PyType_GenericAlloc,
    .tp_new = (newfunc)PythonQoreClass::py_new,
    .tp_free = PyObject_Del,
};

PythonQoreClass::PythonQoreClass(const char* module_name, const QoreClass& qcls) {
    memcpy(&py_type, &static_py_type, sizeof(static_py_type));

    name.sprintf("%s.%s", module_name, qcls.getName());
    py_type.tp_name = name.c_str();
    doc.sprintf("Python wrapper class for Qore class %s", qcls.getName());
    py_type.tp_doc = doc.c_str();

    // create methods
    {
        QoreMethodIterator i(qcls);
        while (i.next()) {
            const QoreMethod* m = i.getMethod();
            if (m->getAccess() > Private) {
                continue;
            }
            QoreStringMaker mdoc("Python wrapper for Qore class method %s::%s()", qcls.getName(), m->getName());
            strvec.push_back(mdoc);
            py_meth_vec.push_back({m->getName(), exec_qore_method, METH_VARARGS, strvec.back().c_str()});
        }
    }

    {
        QoreStaticMethodIterator i(qcls);
        while (i.next()) {
            const QoreMethod* m = i.getMethod();
            if (m->getAccess() > Private) {
                continue;
            }
            QoreStringMaker mdoc("Python wrapper for Qore static class method %s::%s()", qcls.getName(), m->getName());
            strvec.push_back(mdoc);
            py_meth_vec.push_back({m->getName(), exec_qore_static_method, METH_VARARGS, strvec.back().c_str()});
        }
    }

    py_meth_vec.push_back({nullptr, nullptr});
    py_type.tp_methods = &py_meth_vec[0];
    py_type.qcls = &qcls;

    if (PyType_Ready(&py_type) < 0) {
        printd(0, "PythonQoreClass::PythonQoreClass() %s.%s type initialization failed\n", module_name, qcls.getName());
    }
}

PyObject* PythonQoreClass::exec_qore_method(PyObject* self, PyObject* args) {
    QorePythonReferenceHolder selfstr(PyObject_Repr(self));
    assert(PyUnicode_Check(*selfstr));
    QorePythonReferenceHolder argstr(PyObject_Repr(args));
    assert(PyUnicode_Check(*argstr));
    printd(0, "PythonQoreClass::exec_qore_method() self: %p (%s) args: %s\n", self, PyUnicode_AsUTF8(*selfstr), PyUnicode_AsUTF8(*argstr));

    Py_INCREF(Py_None);
    return Py_None;
}

PyObject* PythonQoreClass::exec_qore_static_method(PyObject* self, PyObject* args) {
    QorePythonReferenceHolder selfstr(PyObject_Repr(self));
    assert(PyUnicode_Check(*selfstr));
    QorePythonReferenceHolder argstr(PyObject_Repr(args));
    assert(PyUnicode_Check(*argstr));
    printd(0, "PythonQoreClass::exec_qore_static_method() self: %p (%s) args: %s\n", self, PyUnicode_AsUTF8(*selfstr), PyUnicode_AsUTF8(*argstr));

    Py_INCREF(Py_None);
    return Py_None;
}

PyObject* PythonQoreClass::py_new(QorePyTypeObject* type, PyObject* args, PyObject* kw) {
    QorePythonReferenceHolder argstr(PyObject_Repr(args));
    assert(PyUnicode_Check(*argstr));
    printd(0, "PythonQoreClass::py_new() type: %s (qore: %s) args: %s kw: %p\n", type->tp_name, type->qcls->getName(), PyUnicode_AsUTF8(*argstr), kw);
    PyQoreObject* self = (PyQoreObject*)type->tp_alloc(type, 0);
    self->qobj = nullptr;
    return (PyObject*)self;
}

void PythonQoreClass::py_dealloc(PyQoreObject* self) {
    if (self->qobj) {
        ExceptionSink xsink;
        self->qobj->deref(&xsink);
        self->qobj = nullptr;
    }
    Py_TYPE(self)->tp_free(self);
}

PyObject* PythonQoreClass::py_repr(PyObject* obj) {
    QoreStringMaker str("Qore %s object %p", Py_TYPE(obj)->tp_name, obj);
    return PyUnicode_FromKindAndData(PyUnicode_1BYTE_KIND, str.c_str(), str.size());
}