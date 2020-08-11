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

#include "QoreLoader.h"
#include "QoreMetaPathFinder.h"
#include "QorePythonProgram.h"
#include "PythonQoreClass.h"

#include <memory>

QorePythonManualReferenceHolder QoreLoader::loader_cls;
QorePythonManualReferenceHolder QoreLoader::loader;

static PyMethodDef QoreLoader_methods[] = {
    {"create_module", QoreLoader::create_module, METH_VARARGS, "QoreLoader.create_module() implementation"},
    {"exec_module", QoreLoader::exec_module, METH_VARARGS, "QoreLoader.exec_module() implementation"},
    {nullptr, nullptr},
};

PyDoc_STRVAR(QoreLoader_doc,
"QoreLoader()\n\
\n\
Python modules for Qore code.");

PyTypeObject QoreLoader_Type = {
    PyVarObject_HEAD_INIT(nullptr, 0)
    "QoreLoader",                 // tp_name
    0,                            // tp_basicsize
    0,                            // tp_itemsize
    // Slots
    (destructor)QoreLoader::dealloc,  // tp_dealloc
    0,                            // tp_vectorcall_offset
    0,                            // tp_getattr
    0,                            // tp_setattr
    0,                            // tp_as_async
    QoreLoader::repr,             // tp_repr
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
    QoreLoader_doc,               // tp_doc
    0,                            // tp_traverse
    0,                            // tp_clear
    0,                            // tp_richcompare
    0,                            // tp_weaklistoffset
    0,                            // tp_iter
    0,                            // tp_iternext
    QoreLoader_methods,           // tp_methods
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

int QoreLoader::init() {
    if (PyType_Ready(&QoreLoader_Type) < 0) {
        printd(5, "QoreLoader::init() type initialization failed\n");
        return -1;
    }

    QorePythonReferenceHolder args(PyTuple_New(0));
    loader = PyObject_CallObject((PyObject*)&QoreLoader_Type, *args);
    //loader = PyObject_CallObject((PyObject*)*loader_cls, *args);

    if (PyType_Ready(&PythonQoreException_Type) < 0) {
        return -1;
    }

    return 0;
}

void QoreLoader::del() {
    loader.purge();
    loader_cls.purge();
}

PyObject* QoreLoader::getLoaderRef() {
    assert(*loader);
    Py_INCREF(*loader);
    return *loader;
}

void QoreLoader::dealloc(PyObject* self) {
    //PyObject_GC_UnTrack(self);
    Py_TYPE(self)->tp_free(self);
}

PyObject* QoreLoader::repr(PyObject* obj) {
    QoreStringMaker str("QoreLoader object %p", obj);
    return PyUnicode_FromStringAndSize(str.c_str(), str.size());
}

// class method functions
PyObject* QoreLoader::create_module(PyObject* self, PyObject* args) {
    Py_INCREF(Py_None);
    return Py_None;
}

PyObject* QoreLoader::exec_module(PyObject* self, PyObject* args) {
    QorePythonReferenceHolder argstr(PyObject_Repr(args));
    assert(PyUnicode_Check(*argstr));
    //printd(5, "QoreLoader::exec_module() self: %p args: %s\n", self, PyUnicode_AsUTF8(*argstr));

    assert(PyTuple_Check(args));

    // get module
    // returns a borrowed reference
    PyObject* mod = PyTuple_GetItem(args, 0);
    assert(PyModule_Check(mod));

    // get module name
    assert(PyObject_HasAttrString(mod, "__name__"));
    QorePythonReferenceHolder name(PyObject_GetAttrString(mod, "__name__"));
    assert(PyUnicode_Check(*name));
    const char* name_str = PyUnicode_AsUTF8(*name);

    printd(5, "QoreLoader::exec_module() mod: '%s'\n", name_str);
    QorePythonProgram* qore_python_pgm = QorePythonProgram::getContext();
    //QoreProgram* mod_pgm = QoreMetaPathFinder::getProgram(name_str);
    QoreProgram* mod_pgm = qore_python_pgm->getQoreProgram();
    //printd(5, "QoreLoader::exec_module() qore_python_pgm: %p mod pgm: %p\n", qore_python_pgm, mod_pgm);

    // get root namespace
    const QoreNamespace* ns;
    if (!strcmp(name_str, "qore")) {
        ns = mod_pgm->getQoreNS();
    } else if (!strcmp(name_str, "__root__")) {
        ns = mod_pgm->getRootNS();
    } else {
        ns = getModuleRootNs(name_str, mod_pgm);
    }
    assert(ns);
    //printd(5, "QoreLoader::exec_module() found '%s' NS %p: '::%s'\n", name_str, ns, ns->getName());

    if (ns) {
        QoreProgramContextHelper pch(qore_python_pgm->getQoreProgram());
        qore_python_pgm->importQoreToPython(mod, *ns, name_str);
    }

    Py_INCREF(Py_None);
    return Py_None;
}

const QoreNamespace* QoreLoader::getModuleRootNs(const char* name, QoreProgram* mod_pgm) {
    ReferenceHolder<QoreHashNode> all_mod_info(MM.getModuleHash(), nullptr);
    mod_dep_map_t mod_dep_map;

    // otherwise look for a public namespace and then find the ealiest ancestor provided by the module
    const RootQoreNamespace* root_ns = mod_pgm->getRootNS();
    QoreNamespaceConstIterator i(*root_ns);
    while (i.next()) {
        const QoreNamespace* ns = &i.get();
        const char* mod = ns->getModuleName();
        if (mod && !strcmp(mod, name)) {
            printd(5, "QoreLoader::getModuleRootNs() found '%s'\n", name);
            // try to find parent ns
            while (true) {
                const QoreNamespace* parent = ns->getParent();
                if (!isModule(parent, name, *all_mod_info, mod_dep_map)) {
                    printd(5, "QoreLoader::getModuleRootNs() invalid  parent '%s'\n", parent->getName());
                    break;
                }
                ns = parent;
                printd(5, "QoreLoader::getModuleRootNs() got parent '%s'\n", ns->getName());
            }
            printd(5, "QoreLoader::getModuleRootNs() returning '%s'\n", ns->getName());
            return ns;
        }
    }
    return nullptr;
}

bool QoreLoader::isModule(const QoreNamespace* parent, const char* name, const QoreHashNode* all_mod_info,
        mod_dep_map_t& mod_dep_map) {
    const char* mod = parent->getModuleName();
    if (!mod) {
        return false;
    }
    if (!strcmp(mod, name)) {
        return true;
    }

    if (!all_mod_info) {
        return false;
    }

    const QoreListNode* reexport_list = nullptr;

    // see if we have the reexport list already
    mod_dep_map_t::iterator i = mod_dep_map.lower_bound(mod);
    if (i == mod_dep_map.end() || !strcmp(i->first, mod)) {
        const QoreHashNode* mod_info = all_mod_info->getKeyValue(name).get<QoreHashNode>();
        if (!mod_info) {
            return false;
        }
        reexport_list = mod_info->getKeyValue("reexported-modules").get<QoreListNode>();
        if (!reexport_list) {
            return false;
        }
        mod_dep_map.insert(i, mod_dep_map_t::value_type(mod, reexport_list));
    } else {
        reexport_list = i->second;
    }

    ConstListIterator li(reexport_list);
    while (li.next()) {
        const QoreValue v = li.getValue();
        if (v.getType() == NT_STRING && (*v.get<const QoreStringNode>() == mod)) {
            return true;
        }
    }

    //printd(5, "QoreLoader::isModule() NOT parent: '%s' mod: %s; not in reexport list\n", parent->getName(), mod ? mod : "n/a");
    return false;
}
