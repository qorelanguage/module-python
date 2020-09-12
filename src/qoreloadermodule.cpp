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
#include "PythonQoreCallable.h"

#include <dlfcn.h>

QorePythonProgram* qore_python_pgm = nullptr;

PyDoc_STRVAR(module_doc, "This module provides dynamic access to Qore APIs.");

static int slot_qoreloader_exec(PyObject* m);

static PyObject* qoreloader_load_java(PyObject* self, PyObject* args);
static PyObject* qoreloader_issue_module_cmd(PyObject* self, PyObject* args);

static PyMethodDef qoreloader_methods[] = {
    {"load_java", qoreloader_load_java, METH_VARARGS, "Import the given Java path to the parent Qore program if " \
        "possible; args: import_str (ex: 'org.qore.lang.restclient.*')"},
    {"issue_module_cmd", qoreloader_issue_module_cmd, METH_VARARGS, "Issue the given module command on the given " \
        "module; args: module_name, cmd"},
    {nullptr, nullptr, 0, nullptr},
};

static bool qore_needs_shutdown = false;

thread_local QoreThreadAttacher qoreThreadAttacher;

PyTypeObject PythonQoreObjectBase_Type = {
    PyVarObject_HEAD_INIT(nullptr, 0)
#if !defined(__clang__) && __GNUC__ < 8
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
            qore_python_pgm->doDeref();
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

        init_global_qore_python_pgm();

        if (PyType_Ready(&PythonQoreObjectBase_Type) < 0) {
            return -1;
        }

        if (PyType_Ready(&PythonQoreCallable_Type) < 0) {
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

void init_global_qore_python_pgm() {
    if (!qore_python_pgm) {
        // save and restore the Python thread state while initializing the Qore python module
        PythonThreadStateHelper ptsh;

        QoreThreadAttachHelper attach_helper;
        attach_helper.attach();
        qore_python_pgm = new QorePythonProgram;
    }
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

typedef int (*jni_module_import_t)(ExceptionSink* xsink, QoreProgram* pgm, const char* import);
static jni_module_import_t jni_module_import = nullptr;

PyObject* qoreloader_load_java(PyObject* self, PyObject* args) {
    if (!PyTuple_Check(args) || (PyTuple_Size(args) != 1)) {
        PyErr_SetString(PyExc_ValueError, "single string argument required for 'qoreloader.load_java()'");
        return nullptr;
    }
    PyObject* name = PyTuple_GetItem(args, 0);
    if (!PyUnicode_Check(name)) {
        QoreStringMaker desc("single string string argument required for 'qoreloader.load_java(); got type '%s' " \
            "instead", Py_TYPE(name)->tp_name);
        PyErr_SetString(PyExc_ValueError, desc.c_str());
        return nullptr;
    }
    const char* name_str = PyUnicode_AsUTF8(name);

    {
        QoreString path(name_str);
        if (path.size() > 2 && (path[-2] == '.' && path[-1] == '*')) {
            QoreStringMaker desc("'%s': wildcard imports are not currently supported", name_str);
            PyErr_SetString(PyExc_ValueError, desc.c_str());
            return nullptr;
        }
    }

    ExceptionSink xsink;
    QorePythonProgram* qore_python_pgm = QorePythonProgram::getContext();
    QoreProgram* qpgm = qore_python_pgm->getQoreProgram();
    if (!jni_module_import) {
        if (ModuleManager::runTimeLoadModule("jni", qpgm, &xsink)) {
            qore_python_pgm->raisePythonException(xsink);
            return nullptr;
        }
        jni_module_import = (jni_module_import_t)dlsym(RTLD_DEFAULT, "jni_module_import");
        if (!jni_module_import) {
            PyErr_SetString(PyExc_ValueError, "cannot find required symbol 'jni_module_import'");
            return nullptr;
        }
    }
    //printd(5, "qoreloader_load_java() jni module loaded: import '%s' pgm: %p\n", name_str, qpgm);

    if (MM.runTimeLoadModule("jni", qpgm, &xsink)) {
        qore_python_pgm->raisePythonException(xsink);
        return nullptr;
    }

    // set program context for initialization
    QoreProgramContextHelper pgm_ctx(qpgm);
    if (jni_module_import(&xsink, qpgm, name_str)) {
        assert(xsink);
        qore_python_pgm->raisePythonException(xsink);
        return nullptr;
    }

    Py_INCREF(Py_None);
    return Py_None;
}

PyObject* qoreloader_issue_module_cmd(PyObject* self, PyObject* args) {
    if (!PyTuple_Check(args) || (PyTuple_Size(args) != 2)) {
        PyErr_SetString(PyExc_ValueError, "two string arguments required for 'qoreloader.issue_module_cmd()'");
        return nullptr;
    }
    PyObject* module = PyTuple_GetItem(args, 0);
    if (!PyUnicode_Check(module)) {
        QoreStringMaker desc("first agument must be a string when calling 'qoreloader.issue_module_cmd(); got type " \
            "'%s' instead", Py_TYPE(module)->tp_name);
        PyErr_SetString(PyExc_ValueError, desc.c_str());
        return nullptr;
    }
    PyObject* cmd = PyTuple_GetItem(args, 1);
    if (!PyUnicode_Check(cmd)) {
        QoreStringMaker desc("second agument must be a string when calling 'qoreloader.issue_module_cmd(); got type " \
            "'%s' instead", Py_TYPE(cmd)->tp_name);
        PyErr_SetString(PyExc_ValueError, desc.c_str());
        return nullptr;
    }

    const char* module_str = PyUnicode_AsUTF8(module);
    const char* cmd_str = PyUnicode_AsUTF8(cmd);

    ExceptionSink xsink;
    QorePythonProgram* qore_python_pgm = QorePythonProgram::getContext();
    QoreProgram* qpgm = qore_python_pgm->getQoreProgram();
    if (qpgm->issueModuleCmd(module_str, cmd_str, &xsink)) {
        assert(xsink);
        qore_python_pgm->raisePythonException(xsink);
        return nullptr;
    }
    assert(!xsink);

    Py_INCREF(Py_None);
    return Py_None;
}
