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

#include <memory>

QorePythonReferenceHolder QoreLoader::loader_cls;
QorePythonReferenceHolder QoreLoader::loader;
QoreLoader::meth_vec_t QoreLoader::meth_vec;

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
    0,                            // tp_hash*/
    0,                            // tp_call*/
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
        printd(0, "QoreLoader::init() type initialization failed\n");
        return -1;
    }

    QorePythonReferenceHolder args(PyTuple_New(0));
    loader = PyObject_CallObject((PyObject*)&QoreLoader_Type, *args);
    //loader = PyObject_CallObject((PyObject*)*loader_cls, *args);

    return 0;
}

void QoreLoader::del() {
    loader.purge();
    loader_cls.purge();

    for (auto& i : meth_vec) {
        delete i;
    }
    meth_vec.clear();
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
    return PyUnicode_FromKindAndData(PyUnicode_1BYTE_KIND, str.c_str(), str.size());
}

// class method functions
PyObject* QoreLoader::create_module(PyObject* self, PyObject* args) {
    Py_INCREF(Py_None);
    return Py_None;
}

PyObject* QoreLoader::exec_module(PyObject* self, PyObject* args) {
    QorePythonReferenceHolder argstr(PyObject_Repr(args));
    assert(PyUnicode_Check(*argstr));
    printd(0, "QoreLoader::exec_module() self: %p args: %s\n", self, PyUnicode_AsUTF8(*argstr));

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

    printd(0, "QoreLoader::exec_module() mod: '%s'\n", name_str);
    //QoreProgram* mod_pgm = QoreMetaPathFinder::getProgram(name_str);
    QoreProgram* mod_pgm = qore_python_pgm->getQoreProgram();
    printd(0, "QoreLoader::exec_module() mod pgm: %p\n", mod_pgm);

    // get root namespace
    const QoreNamespace* ns = getModuleRootNs(name_str, mod_pgm->findNamespace("::"));
    printd(0, "QoreLoader::exec_module() found root NS %p: %s\n", ns, ns->getName());

    if (ns) {
        QoreProgramContextHelper pch(qore_python_pgm->getQoreProgram());
        importQoreToPython(mod, *ns);
    }

    Py_INCREF(Py_None);
    return Py_None;
}

const QoreNamespace* QoreLoader::getModuleRootNs(const char* name, const QoreNamespace* root_ns) {
    QoreNamespaceConstIterator i(*root_ns);
    while (i.next()) {
        const QoreNamespace* ns = &i.get();
        const char* mod = ns->getModuleName();
        if (mod && !strcmp(mod, name)) {
            // try to find parent ns
            while (true) {
                const QoreNamespace* parent = ns->getParent();
                mod = parent->getModuleName();
                if (!mod || strcmp(mod, name)) {
                    break;
                }
                ns = parent;
            }
            return ns;
        }
    }
    return nullptr;
}

// import Qore definitions to Python
void QoreLoader::importQoreToPython(PyObject* mod, const QoreNamespace& ns) {
    // import all functions
    QoreNamespaceFunctionIterator fi(ns);
    while (fi.next()) {
        const QoreExternalFunction& func = fi.get();
        // do not import deprecated functions
        if (func.getCodeFlags() & QCF_DEPRECATED) {
            continue;
        }
        importQoreFunctionToPython(mod, func);
    }

    // import all constants
    QoreNamespaceConstantIterator consti(ns);
    while (consti.next()) {
        importQoreConstantToPython(mod, consti.get());
    }

    // import all classes
    QoreNamespaceClassIterator clsi(ns);
    while (clsi.next()) {
        importQoreClassToPython(mod, clsi.get());
    }

    // import all subnamespaces as modules
    QoreNamespaceNamespaceIterator ni(ns);
    while (ni.next()) {
        importQoreNamespaceToPython(mod, ni.get());
    }
}

void QoreLoader::importQoreFunctionToPython(PyObject* mod, const QoreExternalFunction& func) {
    printd(0, "QoreLoader::importQoreFunctionToPython() %s()\n", func.getName());

    QorePythonReferenceHolder capsule(PyCapsule_New((void*)&func, nullptr, nullptr));

    std::unique_ptr<PyMethodDef> funcdef(new PyMethodDef);
    funcdef->ml_name = func.getName();
    funcdef->ml_meth = QoreLoader::callQoreFunction;
    funcdef->ml_flags = METH_VARARGS;
    funcdef->ml_doc = nullptr;

    meth_vec.push_back(funcdef.get());

    QorePythonReferenceHolder pyfunc(PyCFunction_New(funcdef.release(), *capsule));
    PyObject_SetAttrString(mod, func.getName(), pyfunc.release());
}

void QoreLoader::importQoreConstantToPython(PyObject* mod, const QoreExternalConstant& constant) {
    printd(0, "QoreLoader::importQoreConstantToPython() %s()\n", constant.getName());

    ExceptionSink xsink;
    ValueHolder qoreval(constant.getReferencedValue(), &xsink);
    QorePythonReferenceHolder val(qore_python_pgm->getPythonValue(*qoreval, &xsink));
    if (!xsink) {
        PyObject_SetAttrString(mod, constant.getName(), val.release());
    }
}

void QoreLoader::importQoreClassToPython(PyObject* mod, const QoreClass& cls) {
    printd(0, "QoreLoader::importQoreClassToPython() %s()\n", cls.getName());
}

void QoreLoader::importQoreNamespaceToPython(PyObject* mod, const QoreNamespace& ns) {
    printd(0, "QoreLoader::importQoreNamespaceToPython() %s()\n", ns.getName());

    // create a submodule
    QorePythonReferenceHolder new_mod(PyModule_New(ns.getName()));
    importQoreToPython(*new_mod, ns);
    PyObject_SetAttrString(mod, ns.getName(), new_mod.release());
}

// Python integration
PyObject* QoreLoader::callQoreFunction(PyObject* self, PyObject* args) {
    QorePythonReferenceHolder selfstr(PyObject_Repr(self));
    assert(PyUnicode_Check(*selfstr));
    QorePythonReferenceHolder argstr(PyObject_Repr(args));
    assert(PyUnicode_Check(*argstr));
    printd(0, "QoreLoader::callQoreFunction() self: %p (%s) args: %s\n", self, PyUnicode_AsUTF8(*selfstr), PyUnicode_AsUTF8(*argstr));

    assert(PyCapsule_CheckExact(self));
    const QoreExternalFunction* func = reinterpret_cast<QoreExternalFunction*>(PyCapsule_GetPointer(self, nullptr));
    assert(func);

    // get Qore arguments
    ExceptionSink xsink;
    assert(PyTuple_Check(args));
    ReferenceHolder<QoreListNode> qargs(qore_python_pgm->getQoreListFromTuple(&xsink, args), &xsink);
    if (!xsink) {
        ValueHolder rv(func->evalFunction(nullptr, *qargs, qore_python_pgm->getQoreProgram(), &xsink), &xsink);
        if (!xsink) {
            return qore_python_pgm->getPythonValue(*rv, &xsink);
        }
    }

    Py_INCREF(Py_None);
    return Py_None;
}