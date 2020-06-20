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

    //! wraps a Qore object as a Python object of this class
    /** obj will be referenced for the assignment
    */
    DLLLOCAL PyObject* wrap(QoreObject* obj);

    // Python type methods
    DLLLOCAL static PyObject* py_new(QorePyTypeObject* type, PyObject* args, PyObject* kw);
    DLLLOCAL static void py_dealloc(PyQoreObject* self);
    DLLLOCAL static PyObject* py_repr(PyObject* obj);

private:
    QoreString name;
    QoreString doc;

    typedef std::vector<PyMethodDef> py_meth_vec_t;
    py_meth_vec_t py_normal_meth_vec;
    py_meth_vec_t py_static_meth_vec;
    typedef std::vector<QoreString> strvec_t;
    strvec_t strvec;
    typedef std::vector<QorePythonReferenceHolder> py_obj_vec_t;
    py_obj_vec_t py_normal_meth_obj_vec;
    py_obj_vec_t py_static_meth_obj_vec;
    typedef std::set<const char*, ltstr> cstrset_t;

    QorePyTypeObject py_type;

    static PyTypeObject static_py_type;

    DLLLOCAL void populateClass(const QoreClass& qcls, cstrset_t& meth_set);

    DLLLOCAL static PyObject* exec_qore_static_method(const QoreMethod& m, PyObject* args);

    // class methods
    DLLLOCAL static PyObject* exec_qore_method(PyObject* method_capsule, PyObject* args);
    DLLLOCAL static PyObject* exec_qore_static_method(PyObject* method_capsule, PyObject* args);
};

/*
//! Holds a Python exception and restores it on exit
class PythonExceptionHolder {
public:
    DLLLOCAL PythonExceptionHolder() {
        PyErr_Fetch(&error_type, &error_value, &error_traceback);
    }

    DLLLOCAL ~PythonExceptionHolder() {
        PyErr_Restore(error_type, error_value, error_traceback);
    }

protected:
    PyObject* error_type,
        * error_value,
        * error_traceback;
};
*/

DLLLOCAL extern PyTypeObject PythonQoreException_Type;

#endif
