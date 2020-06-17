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

#ifndef _QORE_PYTHON_PYTHONQORECLASS_H

#define _QORE_PYTHON_PYTHONQORECLASS_H

#include "python-module.h"

#include <vector>

// qore object type
struct PyQoreObject {
    PyObject_HEAD
    QoreObject* qobj;
};

struct QorePyTypeObject : public PyTypeObject {
    const QoreClass* qcls = nullptr;
};

class PythonQoreClass {
    friend QorePyTypeObject;
public:
    DLLLOCAL PythonQoreClass(const char* module_name, const QoreClass& qcls);

    DLLLOCAL PyTypeObject* getType() {
        return &py_type;
    }

    // Python type methods
    DLLLOCAL static PyObject* py_new(QorePyTypeObject* type, PyObject* args, PyObject* kw);
    DLLLOCAL static void py_dealloc(PyQoreObject* self);
    DLLLOCAL static PyObject* py_repr(PyObject* obj);

private:
    QoreString name;
    QoreString doc;

    typedef std::vector<PyMethodDef> py_meth_vec_t;
    py_meth_vec_t py_meth_vec;
    typedef std::vector<QoreString> strvec_t;
    strvec_t strvec;

    QorePyTypeObject py_type;

    static PyTypeObject static_py_type;

    // class methods
    DLLLOCAL static PyObject* exec_qore_method(PyObject* self, PyObject* args);
    DLLLOCAL static PyObject* exec_qore_static_method(PyObject* self, PyObject* args);
};

#endif
