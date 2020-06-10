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

class QoreMetaPathFinder {
public:
    //PyObject_HEAD

    //! initializer function
    DLLLOCAL static void init();

    //! type functions
    DLLLOCAL static void dealloc(PyObject* self);
    DLLLOCAL static PyObject* repr(PyObject* obj);
    DLLLOCAL static PyObject* tp_new(PyTypeObject* type, PyObject* args, PyObject* kwds);

    //! class methods
    DLLLOCAL static PyObject* find_spec(PyObject* self, PyObject* args);
};

#endif