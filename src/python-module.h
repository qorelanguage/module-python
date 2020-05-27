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

//! the name of the module
#define QORE_PYTHON_MODULE_NAME "python"
//! the name of the main Python namespace in Qore
#define QORE_PYTHON_NS_NAME "Python"
//! the name of the language in stack traces
#define QORE_PYTHON_LANG_NAME "Python"

DLLLOCAL extern PyThreadState* mainThreadState;

// forward reference
class QorePythonProgram;
class QorePythonClass;

DLLLOCAL extern QorePythonClass* QC_PYTHONBASEOBJECT;
DLLLOCAL extern qore_classid_t CID_PYTHONBASEOBJECT;

/** NOTE: depends on Python internals to work around limitations with the GIL and multiple thread states with multiple
          interpreters
*/
#ifdef HAVE_PYTHON_INTERNAL_INCLUDES
#define Py_BUILD_CORE
#include <internal/pycore_pystate.h>
#else
#include "python_internals.h"
#endif

DLLLOCAL PyThreadState* _qore_PyRuntimeGILState_GetThreadState(_gilstate_runtime_state& gilstate = _PyRuntime.gilstate);
DLLLOCAL PyThreadState* _qore_PyGILState_GetThisThreadState(_gilstate_runtime_state& gilstate = _PyRuntime.gilstate);
DLLLOCAL void _qore_PyGILState_SetThisThreadState(PyThreadState* state, _gilstate_runtime_state& gilstate = _PyRuntime.gilstate);

//! acquires the GIL and sets the main interpreter thread context
/** This class is used when a new interpreter context is created.

    The new interpreter context has its gilstate_counter decremented in the destructor, and the main interpreter
    thread context is restored before releasing the GIL.

    This way we don't need to use the deprecated GIL acquire and release APIs
*/
class QorePythonGilHelper {
public:
    DLLLOCAL QorePythonGilHelper() {
        assert(!PyGILState_Check());
        assert(!_qore_PyRuntimeGILState_GetThreadState());
        assert(!_qore_PyGILState_GetThisThreadState());
        PyEval_AcquireThread(mainThreadState);
        assert(PyThreadState_Get() == mainThreadState);
        // set this thread state
        _qore_PyGILState_SetThisThreadState(mainThreadState);
        assert(_qore_PyGILState_GetThisThreadState() == mainThreadState);
        assert(PyGILState_Check());
    }

    DLLLOCAL ~QorePythonGilHelper() {
        // swap back to the mainThreadState before releasing the GIL
        PyThreadState* state = PyThreadState_Get();
        if (state != mainThreadState) {
            assert(state->gilstate_counter == 1);
            --state->gilstate_counter;
            PyThreadState_Swap(mainThreadState);
        }
        state = _qore_PyGILState_GetThisThreadState();
        if (state != mainThreadState) {
            _qore_PyGILState_SetThisThreadState(mainThreadState);
        }
        assert(PyGILState_Check());
        // release the GIL
        PyEval_ReleaseThread(mainThreadState);
        assert(!PyGILState_Check());
        _qore_PyGILState_SetThisThreadState(nullptr);
    }

protected:
    PyThreadState* state;
};

struct QorePythonThreadInfo {
    PyThreadState* t_state;
    PyGILState_STATE g_state;
    bool valid;
};

//! acquires the GIL and manages thread state
class QorePythonHelper {
public:
    DLLLOCAL QorePythonHelper(const QorePythonProgram* pypgm);

    DLLLOCAL ~QorePythonHelper();

protected:
    const QorePythonProgram* pypgm;
    QorePythonThreadInfo old_state;
};

//! Python ref holder
class QorePythonReferenceHolder {
public:
    DLLLOCAL QorePythonReferenceHolder() : obj(nullptr) {
    }

    DLLLOCAL QorePythonReferenceHolder(PyObject* obj) : obj(obj) {
    }

    DLLLOCAL ~QorePythonReferenceHolder() {
        purge();
    }

    DLLLOCAL void purge() {
        if (obj) {
            Py_DECREF(obj);
            obj = nullptr;
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
