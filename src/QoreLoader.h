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

#ifndef _QORE_PYTHON_QORELOADER_H

#define _QORE_PYTHON_QORELOADER_H

#include "python-module.h"

#include <vector>

// forward references
class PythonQoreClass;

class QoreLoader {
public:
    //! initializer function
    DLLLOCAL static int init();

    DLLLOCAL static void del();

    //! returns the loader object with the reference count incremented
    DLLLOCAL static PyObject* getLoaderRef();

    //! type functions
    DLLLOCAL static void dealloc(PyObject* self);
    DLLLOCAL static PyObject* repr(PyObject* obj);

    //! class methods
    DLLLOCAL static PyObject* create_module(PyObject* self, PyObject* args);
    DLLLOCAL static PyObject* exec_module(PyObject* self, PyObject* args);

    // Python integration
    DLLLOCAL static PyObject* callQoreFunction(PyObject* self, PyObject* args);

private:
    typedef std::vector<PyMethodDef*> meth_vec_t;
    static meth_vec_t meth_vec;
    typedef std::vector<PythonQoreClass*> py_cls_vec_t;
    static py_cls_vec_t py_cls_vec;

    DLLLOCAL static QorePythonReferenceHolder loader_cls;
    DLLLOCAL static QorePythonReferenceHolder loader;

    // registers Python Qore class
    DLLLOCAL static void registerPythonClass(PythonQoreClass* py_cls);

    DLLLOCAL static void importQoreToPython(PyObject* mod, const QoreNamespace& ns, const char* mod_name);
    DLLLOCAL static void importQoreFunctionToPython(PyObject* mod, const QoreExternalFunction& func);
    DLLLOCAL static void importQoreConstantToPython(PyObject* mod, const QoreExternalConstant& constant);
    DLLLOCAL static void importQoreClassToPython(PyObject* mod, const QoreClass& cls, const char* mod_name);
    DLLLOCAL static void importQoreNamespaceToPython(PyObject* mod, const QoreNamespace& ns);

    DLLLOCAL static const QoreNamespace* getModuleRootNs(const char* name, const QoreNamespace* root_ns);
};

#endif
