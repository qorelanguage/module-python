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

#include "QoreMetaPathFinder.h"
#include "QoreLoader.h"
#include "QorePythonProgram.h"

QorePythonManualReferenceHolder QoreMetaPathFinder::qore_package;
QorePythonManualReferenceHolder QoreMetaPathFinder::mod_spec_cls;

PyDoc_STRVAR(QoreMetaPathFinder_doc,
"QoreMetaPathFinder()\n\
\n\
Creates Python wrappers for Qore code.");

static PyMethodDef QoreMetaPathFinder_methods[] = {
    {"find_spec", QoreMetaPathFinder::find_spec, METH_VARARGS, "QoreMetaPathFinder.find_spec() implementation"},
    {nullptr, nullptr},
};

PyTypeObject QoreMetaPathFinder_Type = {
    PyVarObject_HEAD_INIT(nullptr, 0)
    "QoreMetaPathFinder",         // tp_name
    0,                            // tp_basicsize
    0,                            // tp_itemsize
    // Slots
    (destructor)QoreMetaPathFinder::dealloc,  // tp_dealloc
    0,                            // tp_vectorcall_offset
    0,                            // tp_getattr
    0,                            // tp_setattr
    0,                            // tp_as_async
    QoreMetaPathFinder::repr,     // tp_repr
    0,                            // tp_as_number
    0,                            // tp_as_sequence
    0,                            // tp_as_mapping
    0,                            // tp_hash
    0,                            // tp_call
    0,                            // tp_str
    PyObject_GenericGetAttr,      // tp_getattro
    0,                            // tp_setattro
    0,                            // tp_as_buffer
    Py_TPFLAGS_DEFAULT,           // tp_flags
    QoreMetaPathFinder_doc,       // tp_doc
    0,                            // tp_traverse
    0,                            // tp_clear
    0,                            // tp_richcompare
    0,                            // tp_weaklistoffset
    0,                            // tp_iter
    0,                            // tp_iternext
    QoreMetaPathFinder_methods,   // tp_methods
    0,                            // tp_members
    0,                            // tp_getset
    &PyBaseObject_Type,           // tp_base
    0,                            // tp_dict
    0,                            // tp_descr_get
    0,                            // tp_descr_set
    0,                            // tp_dictoffset
    0,                            // tp_init
    PyType_GenericAlloc,          // tp_alloc
    PyType_GenericNew,            // tp_new
    PyObject_Del,                 // tp_free
};

int QoreMetaPathFinder::init() {
    if (PyType_Ready(&QoreMetaPathFinder_Type) < 0) {
        printd(5, "QoreMetaPathFinder::init() type initialization failed\n");
        return -1;
    }

    // get importlib.machinery.ModuleSpec class
    QorePythonReferenceHolder mod(PyImport_ImportModule("importlib.machinery"));
    if (!*mod) {
        printd(5, "QoreMetaPathFinder::init() ERROR: no importlib.machinery module\n");
        return -1;
    }

    if (!PyObject_HasAttrString(*mod, "ModuleSpec")) {
        printd(5, "QoreMetaPathFinder::init() ERROR: no ModuleSpec class in importlib.machinery\n");
        return -1;
    }

    mod_spec_cls = PyObject_GetAttrString(*mod, "ModuleSpec");
    printd(5, "mod_spec_cls: %p %s\n", *mod_spec_cls, Py_TYPE(*mod_spec_cls)->tp_name);

    return 0;
}

int QoreMetaPathFinder::setupModules() {
    QorePythonReferenceHolder mpf(PyObject_CallObject((PyObject*)&QoreMetaPathFinder_Type, nullptr));
    printd(5, "QoreMetaPathFinder::setupModules() created finder %p\n", *mpf);

    // get sys module
    QorePythonReferenceHolder mod(PyImport_ImportModule("sys"));
    if (!*mod) {
        printd(5, "QoreMetaPathFinder::setupModules() ERROR: no sys module\n");
        return -1;
    }

    if (!PyObject_HasAttrString(*mod, "meta_path")) {
        printd(5, "QoreMetaPathFinder::setupModules() ERROR: no sys.meta_path\n");
        return -1;
    }

    QorePythonReferenceHolder meta_path(PyObject_GetAttrString(*mod, "meta_path"));
    printd(5, "meta_path: %p %s\n", *meta_path, Py_TYPE(*meta_path)->tp_name);
    if (!PyList_Check(*meta_path)) {
        printd(5, "QoreMetaPathFinder::setupModules() ERROR: sys.meta_path is not a list\n");
        return -1;
    }

    // insert object
    if (PyList_Append(*meta_path, *mpf)) {
        printd(5, "QoreMetaPathFinder::setupModules() ERROR: failed to append finder to sys.meta_path\n");
        return -1;
    }

    return 0;
}

void QoreMetaPathFinder::del() {
    qore_package.release();
    mod_spec_cls.purge();
}

void QoreMetaPathFinder::dealloc(PyObject* self) {
    //PyObject_GC_UnTrack(self);
    Py_TYPE(self)->tp_free(self);
}

PyObject* QoreMetaPathFinder::repr(PyObject* obj) {
    QoreStringMaker str("QoreMetaPathFinder object %p", obj);
    return PyUnicode_FromKindAndData(PyUnicode_1BYTE_KIND, str.c_str(), str.size());
}

// class method functions
PyObject* QoreMetaPathFinder::find_spec(PyObject* self, PyObject* args) {
    Py_ssize_t len = PyTuple_Size(args);
    {
        // show args
        QorePythonReferenceHolder argstr(PyObject_Repr(args));
        assert(PyUnicode_Check(*argstr));
        printd(5, "QoreMetaPathFinder::find_spec() args: %s\n", PyUnicode_AsUTF8(*argstr));
    }

    // returns a borrowed reference
    PyObject* fullname = PyTuple_GetItem(args, 0);
    assert(PyUnicode_Check(fullname));
    const char* fname = PyUnicode_AsUTF8(fullname);
    PyObject* path = PyTuple_GetItem(args, 1);

    // return qore top-level package module spec
    if (path == Py_None) {
        if (!strcmp(fname, "qore")) {
            PyObject* rv = getQorePackageModuleSpec();
            if (rv) {
                return rv;
            }
        }
    } else {
        QoreString mname(fname);
        if (mname.size() > 5 && mname.equalPartial("qore.")) {
            mname.replace(0, 5, (const char*)nullptr);
            PyObject* rv = tryLoadModule(mname);
            if (rv) {
                return rv;
            }
        }
    }

    Py_INCREF(Py_None);
    return Py_None;
}

PyObject* QoreMetaPathFinder::newModuleSpec(const QoreString& name, PyObject* loader) {
    // create args for ModuleSpec constructor
    QorePythonReferenceHolder args(PyTuple_New(2));
    PyTuple_SET_ITEM(*args, 0, PyUnicode_FromKindAndData(PyUnicode_1BYTE_KIND, name.c_str(), name.size()));

    if (!loader) {
        Py_INCREF(Py_None);
        PyTuple_SET_ITEM(*args, 1, Py_None);
    } else {
        PyTuple_SET_ITEM(*args, 1, loader);
    }

    QorePythonReferenceHolder mod(PyObject_CallObject((PyObject*)*mod_spec_cls, *args));
    assert(mod);
    return mod.release();
}

PyObject* QoreMetaPathFinder::getQorePackageModuleSpec() {
    if (!qore_package) {
        // create qore package
        qore_package = newModuleSpec("qore");

        QorePythonReferenceHolder search_locations(PyList_New(0));
        //PyList_SET_ITEM(*search_locations, 0, PyUnicode_FromKindAndData(PyUnicode_1BYTE_KIND, "test", 4));
        PyObject_SetAttrString(*qore_package, "submodule_search_locations", *search_locations);
    }

    qore_package.py_ref();
    //printd(5, "QoreMetaPathFinder::getQorePackageModuleSpec() returning existing qore_package: %p\n", *qore_package);
    return *qore_package;
}

PyObject* QoreMetaPathFinder::tryLoadModule(const QoreString& mname) {
    printd(5, "QoreMetaPathFinder::tryLoadModule() load '%s'\n", mname.c_str());

    ExceptionSink xsink;
    if (ModuleManager::runTimeLoadModule(mname.c_str(), qore_python_pgm->getQoreProgram(), &xsink)) {
        return nullptr;
    }

    return newModuleSpec(mname, QoreLoader::getLoaderRef());
}