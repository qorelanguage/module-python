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
#if PY_MAJOR_VERSION >= 3
#if PY_MINOR_VERSION == 9
#include "python39_internals.h"
#elif PY_MINOR_VERSION == 8
#include "python38_internals.h"
#elif PY_MINOR_VERSION == 7
#include "python37_internals.h"
#else
#error unsupported python version
#endif
#endif
#endif

/** Thread State Locations:
    - _PyRuntime.gilstate.tstate_current - must only be modified while holding the GIL
        read: _qore_PyRuntimeGILState_GetThreadState (_PyThreadState_GET)
        write: PyThreadState_Swap

    - _PyRuntime.ceval.gil.last_holder - must only be modified while holding the GIL
        read: _qore_PyCeval_GetThreadState()
        write: _qore_PyCeval_SwapThreadState()

    - TSS thread state - thread local
        read: PyGILState_GetThisThreadState()
        write: _qore_PyGILState_SetThisThreadState()
*/

//! acquires the GIL and sets the main interpreter thread context
/** This class is used when a new interpreter context is created.

    The new interpreter context has its gilstate_counter decremented in the destructor, and the main interpreter
    thread context is restored before releasing the GIL.

    This way we don't need to use the deprecated GIL acquire and release APIs
*/
class QorePythonGilHelper {
public:
    DLLLOCAL QorePythonGilHelper(PyThreadState* new_thread_state = mainThreadState);

    DLLLOCAL ~QorePythonGilHelper();

    DLLLOCAL void set(PyThreadState* other_state);

protected:
    PyThreadState* new_thread_state;
    PyThreadState* state;
    PyThreadState* t_state;
    bool release_gil = true;
};

class QorePythonReleaseGilHelper {
public:
    DLLLOCAL QorePythonReleaseGilHelper() : _save(PyEval_SaveThread()) {
    }

    DLLLOCAL ~QorePythonReleaseGilHelper() {
        PyEval_RestoreThread(_save);
    }

private:
    PyThreadState* _save;
};

struct QorePythonThreadInfo {
    PyThreadState* tss_state;
    PyThreadState* t_state;
    PyThreadState* ceval_state;
    PyGILState_STATE g_state;
    int recursion_depth;
    bool valid;
};

//! acquires the GIL and manages thread state
class QorePythonHelper {
public:
    DLLLOCAL QorePythonHelper(const QorePythonProgram* pypgm);

    DLLLOCAL ~QorePythonHelper();

protected:
    void* old_pgm;
    QorePythonThreadInfo old_state;
    const QorePythonProgram* new_pypgm;
};

class QorePythonManualReferenceHolder {
public:
    DLLLOCAL QorePythonManualReferenceHolder() : obj(nullptr) {
    }

    DLLLOCAL QorePythonManualReferenceHolder(PyObject* obj) : obj(obj) {
    }

    DLLLOCAL QorePythonManualReferenceHolder(QorePythonManualReferenceHolder&& old) : obj(old.obj) {
        old.obj = nullptr;
    }

    DLLLOCAL void purge() {
        if (obj) {
            py_deref();
            obj = nullptr;
        }
    }

    DLLLOCAL QorePythonManualReferenceHolder& operator=(PyObject* obj) {
        if (this->obj) {
            py_deref();
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

    DLLLOCAL PyObject* operator*() const {
        return obj;
    }

    DLLLOCAL operator bool() const {
        return (bool)obj;
    }

    DLLLOCAL void py_ref() {
        assert(obj);
        Py_INCREF(obj);
    }

    DLLLOCAL void py_deref() {
        assert(obj);
        Py_DECREF(obj);
    }

protected:
    PyObject* obj;

private:
    QorePythonManualReferenceHolder(const QorePythonManualReferenceHolder&) = delete;
    QorePythonManualReferenceHolder& operator=(QorePythonManualReferenceHolder&) = delete;
};

//! Python ref holder
class QorePythonReferenceHolder : public QorePythonManualReferenceHolder {
public:
    DLLLOCAL QorePythonReferenceHolder() : QorePythonManualReferenceHolder(nullptr) {
    }

    DLLLOCAL QorePythonReferenceHolder(PyObject* obj) : QorePythonManualReferenceHolder(obj) {
    }

    DLLLOCAL QorePythonReferenceHolder(QorePythonReferenceHolder&& old) : QorePythonManualReferenceHolder(std::move(old)) {
    }

    DLLLOCAL ~QorePythonReferenceHolder() {
        purge();
    }

    DLLLOCAL QorePythonReferenceHolder& operator=(PyObject* obj) {
        if (this->obj) {
            py_deref();
        }
        this->obj = obj;
        return *this;
    }
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

// Base type for Qore objects in Python
DLLLOCAL extern PyTypeObject PythonQoreObjectBase_Type;

// Python program control for Qore interfacing
DLLLOCAL extern QorePythonProgram* qore_python_pgm;

// module registration function
DLLEXPORT extern "C" void python_qore_module_desc(QoreModuleInfo& mod_info);

//! Python module definition function for the qoreloader module
DLLLOCAL PyMODINIT_FUNC PyInit_qoreloader();

//! Creates the global QOre Python program object
DLLLOCAL void init_global_qore_python_pgm();

class QorePythonProgram;
DLLLOCAL extern QoreNamespace PNS;

DLLLOCAL extern int python_u_tld_key;
DLLLOCAL extern int python_qobj_key;

class QorePythonImplicitQoreArgHelper {
public:
    DLLLOCAL QorePythonImplicitQoreArgHelper(void* obj)
        : old_ptr(q_swap_thread_local_data(python_qobj_key, (void*)obj)) {
    }

    DLLLOCAL ~QorePythonImplicitQoreArgHelper() {
        q_swap_thread_local_data(python_qobj_key, old_ptr);
    }

    DLLLOCAL static QoreObject* getQoreObject() {
        return (QoreObject*)q_get_thread_local_data(python_qobj_key);
    }

    DLLLOCAL static ResolvedCallReferenceNode* getQoreCallable() {
        return (ResolvedCallReferenceNode*)q_get_thread_local_data(python_qobj_key);
    }

private:
    void* old_ptr;
};

class PythonQoreClass;
typedef std::map<const QoreClass*, PythonQoreClass*> py_cls_map_t;

#if 0
DLLLOCAL int q_reset_python(ExceptionSink* xsink);
#endif

#endif
