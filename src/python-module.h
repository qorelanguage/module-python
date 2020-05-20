/* -*- mode: c++; indent-tabs-mode: nil -*- */
/*
    python-module.h

    Qore Programming Language

    Copyright 2020 Qore Technologies, s.r.o.

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

#ifndef _QORE_PYTHON_MODULE_H
#define _QORE_PYTHON_MODULE_H

#define PY_SSIZE_T_CLEAN
#include <Python.h>
#include <node.h>

#include <qore/Qore.h>

//! acquires the GIL and manages thread state
class QorePythonHelper {
public:
    DLLLOCAL QorePythonHelper(PyThreadState* python) : old_python(python) {
        old_python = PyThreadState_Swap(python);
        PyEval_AcquireLock();
        assert(PyGILState_Check());
    }

    DLLLOCAL ~QorePythonHelper() {
        PyEval_ReleaseLock();
        PyThreadState_Swap(old_python);
    }

protected:
    PyThreadState* old_python;
};

//! acquires the GIL
class QorePythonGilHelper {
public:
    DLLLOCAL QorePythonGilHelper() {
        PyEval_AcquireLock();
        assert(PyGILState_Check());
    }

    DLLLOCAL ~QorePythonGilHelper() {
        PyEval_ReleaseLock();
    }

protected:
};

//! Python ref holder
class QorePythonReferenceHolder {
public:
    DLLLOCAL QorePythonReferenceHolder() : obj(nullptr) {
    }

    DLLLOCAL QorePythonReferenceHolder(PyObject* obj) : obj(obj) {
    }

    DLLLOCAL ~QorePythonReferenceHolder() {
        if (obj) {
            Py_DECREF(obj);
        }
    }

    DLLLOCAL QorePythonReferenceHolder& operator=(PyObject* obj) {
        if (this->obj) {
            Py_DECREF(this->obj);
        }
        this->obj = obj;
        return *this;
    }

    DLLLOCAL PyObject* release() {
        PyObject* rv = obj;
        obj = nullptr;
        return rv;
    }

    DLLLOCAL PyObject** getRef() {
        return &obj;
    }

    DLLLOCAL PyObject* operator*() {
        return obj;
    }

    DLLLOCAL operator bool() const {
        return (bool)obj;
    }

protected:
    PyObject* obj;

private:
    QorePythonReferenceHolder(const QorePythonReferenceHolder&) = delete;
    QorePythonReferenceHolder& operator=(QorePythonReferenceHolder&) = delete;
};

class QorePythonGilStateHelper {
public:
    DLLLOCAL QorePythonGilStateHelper() {
        old_state = PyGILState_Ensure();
    }

    DLLLOCAL ~QorePythonGilStateHelper() {
        PyGILState_Release(old_state);
    }

protected:
    PyGILState_STATE old_state;
};

class QorePythonNodeHolder {
public:
    DLLLOCAL QorePythonNodeHolder(_node* node) : node(node) {
    }

    DLLLOCAL ~QorePythonNodeHolder() {
        if (node) {
            PyNode_Free(node);
        }
    }

    DLLLOCAL _node* release() {
        _node* rv = node;
        node = nullptr;
        return rv;
    }

    DLLLOCAL operator bool() const {
        return (bool)node;
    }

    DLLLOCAL _node* operator*() {
        return node;
    }

    DLLLOCAL const _node* operator*() const {
        return node;
    }

protected:
    _node* node;
};

class QorePythonProgram;
DLLLOCAL extern QoreNamespace PNS;

#endif
