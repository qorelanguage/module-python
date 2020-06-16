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

#ifndef _QORE_PYTHON_QOREMETAPATHFINDER_H

#define _QORE_PYTHON_QOREMETAPATHFINDER_H

#include "python-module.h"

#include <map>

/*
struct ModInfo {
    PyObject* spec;
    QoreProgram* pgm;
};
*/

class QoreMetaPathFinder {
public:
    //! initializer function
    DLLLOCAL static int init();

    //! destructor function
    DLLLOCAL static void del();

    //DLLLOCAL static QoreProgram* getProgram(const char* mod);

    //! type functions
    DLLLOCAL static void dealloc(PyObject* self);
    DLLLOCAL static PyObject* repr(PyObject* obj);

    //! class methods
    DLLLOCAL static PyObject* find_spec(PyObject* self, PyObject* args);

private:
    DLLLOCAL static QoreThreadLock m;
    DLLLOCAL static QorePythonReferenceHolder qore_package;
    DLLLOCAL static QorePythonReferenceHolder mod_spec_cls;

    /*
    typedef std::map<std::string, ModInfo> mod_map_t;
    DLLLOCAL static mod_map_t mod_map;
    */

    DLLLOCAL static PyObject* getQorePackageModuleSpec();
    DLLLOCAL static PyObject* tryLoadModule(const QoreString& mname);
};

//! save and restore the Python thread state
class PythonThreadStateHelper {
public:
    DLLLOCAL PythonThreadStateHelper() : thisThreadState(PyThreadState_Get()) {
        assert(PyGILState_Check());
    }

    DLLLOCAL ~PythonThreadStateHelper() {
        PyThreadState_Swap(nullptr);
        PyEval_AcquireThread(thisThreadState);
        _qore_PyGILState_SetThisThreadState(thisThreadState);
    }

private:
    PyThreadState* thisThreadState;
};

#endif
