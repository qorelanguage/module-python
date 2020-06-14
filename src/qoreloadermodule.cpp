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

QoreProgram* python_pgm = nullptr;

PyDoc_STRVAR(module_doc, "This module provides dynamic access to Qore APIs.");

static PyMethodDef qoreloader_methods[] = {};

static int slot_qoreloader_exec(PyObject* m);

static bool qore_needs_shutdown = false;

thread_local QoreThreadAttacher qoreThreadAttacher;

void qoreloader_free(void* obj) {
    printd(0, "qoreloader_free() obj: %p qore_needs_shutdown: %d\n", obj, qore_needs_shutdown);

    if (python_pgm) {
        ExceptionSink xsink;
        python_pgm->waitForTerminationAndDeref(&xsink);
        python_pgm = nullptr;
    }

    QoreMetaPathFinder::del();
    QoreLoader::del();

    if (qore_needs_shutdown) {
        qore_cleanup();
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
    printd(0, "slot_qoreloader_exec()\n");
    // initialize qore library if necessary
    if (!q_libqore_initalized()) {
        qore_init(QL_MIT);
        qore_needs_shutdown = true;
        printd(0, "PyInit_qoreloader() Qore library initialized\n");
    }

    {
        assert(!python_pgm);
        // save and restore the Python thread state while initializing the Qore python module
        PythonThreadStateHelper ptsh;

        QoreThreadAttachHelper attach_helper;
        attach_helper.attach();
        python_pgm = new QoreProgram;
        // ensure that the jni module symbols are loaded into the new Program object
        ExceptionSink xsink;
        MM.runTimeLoadModule("python", python_pgm, &xsink);
        if (xsink) {
            return -1;
        }
    }

    if (QoreLoader::init()) {
        return -1;
    }

    if (QoreMetaPathFinder::init()) {
        return -1;
    }
    return 0;
}

PyMODINIT_FUNC PyInit_qoreloader(void) {
    return PyModuleDef_Init(&qoreloadermodule);
}
