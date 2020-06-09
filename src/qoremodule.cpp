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

PyDoc_STRVAR(module_doc, "This module provides dynamic access to Qore APIs.");

static PyMethodDef qore_methods[] = {};

static int slot_qore_exec(PyObject* m);

static struct PyModuleDef_Slot qore_slots[] = {
    {Py_mod_exec, reinterpret_cast<void*>(slot_qore_exec)},
    {0, nullptr},
};

static struct PyModuleDef qoremodule = {
    PyModuleDef_HEAD_INIT,
    "qore",
    module_doc,
    0,
    qore_methods,
    qore_slots,
    nullptr,
    nullptr,
    nullptr
};

static int slot_qore_exec(PyObject *m) {
    printf("slot_qore_exec()\n");
    return 0;
}

PyMODINIT_FUNC PyInit_qore(void) {
    return PyModuleDef_Init(&qoremodule);
}
