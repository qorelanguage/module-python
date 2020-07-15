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
#include "QoreThreadAttachHelper.h"
#include "QoreLoader.h"
#include "QoreMetaPathFinder.h"
#include "QorePythonProgram.h"

QorePythonProgram* qore_python_pgm = nullptr;

PyDoc_STRVAR(module_doc, "This module provides dynamic access to Qore APIs.");

static int slot_qoreloader_exec(PyObject* m);

static PyMethodDef qoreloader_methods[] = {
    {nullptr, nullptr, 0, nullptr},
};

static bool qore_needs_shutdown = false;

thread_local QoreThreadAttacher qoreThreadAttacher;

PyTypeObject PythonQoreObjectBase_Type = {
    PyVarObject_HEAD_INIT(nullptr, 0)
#if !defined(__clang__) && __GNUC__ < 6
    // g++ 5.4.0 does not accept the short-form initialization below :(
    "PythonQoreObjectBase",         // tp_name
    0,                              // tp_basicsize
    0,                              // tp_itemsize
    nullptr,                        // tp_dealloc
    0,                              // tp_vectorcall_offset/
    0,                              // tp_getattr
    0,                              // tp_setattr
    0,                              // tp_as_async
    nullptr,                        // tp_repr
    0,                              // tp_as_number
    0,                              // tp_as_sequence
    0,                              // tp_as_mapping
    0,                              // tp_hash
    0,                              // tp_call
    0,                              // tp_str
    0,                              // tp_getattro
    0,                              // tp_setattro
    0,                              // tp_as_buffer
    Py_TPFLAGS_DEFAULT | Py_TPFLAGS_BASETYPE,              // tp_flags
    "base class for Python objects based on Qore classes", // tp_doc
    0,                              // tp_traverse
    0,                              // tp_clear
    0,                              // tp_richcompare
    0,                              // tp_weaklistoffset
    0,                              // tp_iter
    0,                              // tp_iternext
    0,                              // tp_methods
    0,                              // tp_members
    0,                              // tp_getset
    0,                              // tp_base
    0,                              // tp_dict
    0,                              // tp_descr_get
    0,                              // tp_descr_set
    0,                              // tp_dictoffset
    0,                              // tp_init
    0,                              // tp_alloc
    0,                              // tp_new
#else
    .tp_name = "PythonQoreObjectBase",
    .tp_flags = Py_TPFLAGS_DEFAULT | Py_TPFLAGS_BASETYPE,
    .tp_doc = "base class for Python objects based on Qore classes",
#endif
};

static int init_count = 0;

void qoreloader_free(void* obj) {
    if (!--init_count) {
        printd(5, "qoreloader_free() obj: %p qore_needs_shutdown: %d\n", obj, qore_needs_shutdown);

        if (qore_python_pgm) {
            ExceptionSink xsink;
            qore_python_pgm->deref(&xsink);
            qore_python_pgm = nullptr;
        }

        QoreMetaPathFinder::del();
        QoreLoader::del();

        if (qore_needs_shutdown) {
            qore_cleanup();
        }
    }
}

static struct PyModuleDef_Slot qoreloader_slots[] = {
    {Py_mod_exec, reinterpret_cast<void*>(slot_qoreloader_exec)},
    {0, nullptr},
};

static struct PyModuleDef qoreloadermodule = {
    PyModuleDef_HEAD_INIT,
    "qoreloader",        // m_name
    module_doc,          // m_doc
    0,                   // m_size
    qoreloader_methods,  // m_methods
    qoreloader_slots,    // m_slots
    nullptr,             // m_traverse
    nullptr,             // m_clear
    qoreloader_free,     // m_free
};

static int slot_qoreloader_exec(PyObject *m) {
    ++init_count;
    //printd(5, "slot_qoreloader_exec() ic: %d\n", init_count);
    if (init_count == 1) {
        // initialize qore library if necessary
        if (!q_libqore_initalized()) {
            qore_init(QL_MIT);
            qore_needs_shutdown = true;
            printd(5, "PyInit_qoreloader() Qore library initialized\n");

            // ensure that the Qore python module is registered as a Qore module
            ExceptionSink xsink;
            MM.runTimeLoadModule(&xsink, "python", nullptr, python_qore_module_desc);
            if (xsink) {
                return -1;
            }
        }

        if (!qore_python_pgm) {
            // save and restore the Python thread state while initializing the Qore python module
            PythonThreadStateHelper ptsh;

            QoreThreadAttachHelper attach_helper;
            attach_helper.attach();
            qore_python_pgm = new QorePythonProgram;
        }

        if (PyType_Ready(&PythonQoreObjectBase_Type) < 0) {
            return -1;
        }

        if (QoreLoader::init()) {
            return -1;
        }

        if (QoreMetaPathFinder::init()) {
            return -1;
        }
    }

    QoreMetaPathFinder::setupModules();

    return 0;
}

PyMODINIT_FUNC PyInit_qoreloader() {
    // first check if the runtime library differs from the dynamically-linked one
    if (!Py_TYPE(PyExc_RuntimeError)) {
        fprintf(stderr, "ERROR: the Python runtime library is different than the dynamically linked one; it's not " \
            "possible to raise a Python exception in case without a crash; aborting\n");
        return nullptr;
    }

    const char* ver = Py_GetVersion();
    if (!ver) {
        fprintf(stderr, "cannot determine Python version; no value returned from Py_GetVersion()'\n");
        return nullptr;
    }
    const char* p = strchr(ver, '.');
    if (!p || p == ver) {
        fprintf(stderr, "cannot determine Python major version from '%s'\n", ver);
        return nullptr;
    }
    int major = atoi(ver);
    int minor = atoi(p + 1);

    if (major != PY_MAJOR_VERSION || minor != PY_MINOR_VERSION) {
        fprintf(stderr, "cannot load the qoreloader module; compiled with '%d.%d.%d'; runtime version is '%s'\n",
            PY_MAJOR_VERSION, PY_MINOR_VERSION, PY_MICRO_VERSION, ver);
        return nullptr;
    }

    return PyModuleDef_Init(&qoreloadermodule);
}
