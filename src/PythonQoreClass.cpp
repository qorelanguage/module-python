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

#include "PythonQoreClass.h"
#include "QoreLoader.h"
#include "QorePythonProgram.h"

#include <string.h>
#include <memory>
#include <set>

#include <frameobject.h>

static int qore_exception_init(PyObject* self, PyObject* args, PyObject* kwds) {
    assert(PyTuple_Check(args));
    Py_ssize_t size = PyTuple_Size(args);
    if (!size) {
        return -1;
    }
    PyObject_SetAttrString(self, "err", PyTuple_GetItem(args, 0));
    if (size > 1) {
        PyObject_SetAttrString(self, "desc", PyTuple_GetItem(args, 1));
        if (size > 2) {
            PyObject_SetAttrString(self, "arg", PyTuple_GetItem(args, 2));
        }
    }
    return 0;
}

PyTypeObject PythonQoreException_Type = {
    PyVarObject_HEAD_INIT(nullptr, 0)
    .tp_name = "QoreException",
    .tp_basicsize = sizeof(PyBaseExceptionObject),
    .tp_flags = Py_TPFLAGS_DEFAULT,
    .tp_base = reinterpret_cast<PyTypeObject*>(PyExc_Exception),
    .tp_doc = "Qore exception class",
    .tp_init = qore_exception_init,
};

PyTypeObject PythonQoreClass::static_py_type = {
    PyVarObject_HEAD_INIT(nullptr, 0)
    .tp_basicsize = sizeof(PyQoreObject),
    .tp_dealloc = (destructor)PythonQoreClass::py_dealloc,
    .tp_repr = PythonQoreClass::py_repr,
    .tp_getattro = PyObject_GenericGetAttr,
    .tp_flags = Py_TPFLAGS_DEFAULT,
    .tp_base = &PythonQoreObjectBase_Type,
    .tp_alloc = PyType_GenericAlloc,
    .tp_new = (newfunc)PythonQoreClass::py_new,
    .tp_free = PyObject_Del,
};

static bool PyQoreObject_Check(PyObject* obj) {
    return obj && Py_TYPE(obj)->tp_base == &PythonQoreObjectBase_Type;
}

PythonQoreClass::PythonQoreClass(const char* module_name, const QoreClass& qcls) {
    memcpy(&py_type, &static_py_type, sizeof(static_py_type));

    name.sprintf("%s.%s", module_name, qcls.getName());
    py_type.tp_name = name.c_str();
    doc.sprintf("Python wrapper class for Qore class %s", qcls.getName());
    py_type.tp_doc = doc.c_str();
    py_type.tp_dict = PyDict_New();

    // set of normal method names
    cstrset_t meth_set;

    // setup class
    populateClass(qcls, meth_set);
    QoreParentClassIterator ci(qcls);
    while (ci.next()) {
        if (ci.getAccess() > Private) {
            continue;
        }
        populateClass(ci.getParentClass(), meth_set);
    }

    // add normal methods
    for (size_t i = 0; i < py_normal_meth_vec.size(); ++i) {
        PyMethodDef& md = py_normal_meth_vec[i];
        QorePythonReferenceHolder method_capsule(py_normal_meth_obj_vec[i].release());
        QorePythonReferenceHolder func(PyCFunction_New(&md, *method_capsule));
        QorePythonReferenceHolder meth(PyInstanceMethod_New(*func));
        const QoreMethod* m = reinterpret_cast<const QoreMethod*>(PyCapsule_GetPointer(*method_capsule, nullptr));
        PyDict_SetItemString(py_type.tp_dict, m->getName(), *meth);
    }
    py_normal_meth_obj_vec.clear();

    // add static methods
    for (size_t i = 0; i < py_static_meth_vec.size(); ++i) {
        PyMethodDef& md = py_static_meth_vec[i];
        QorePythonReferenceHolder method_capsule(py_static_meth_obj_vec[i].release());
        QorePythonReferenceHolder func(PyCFunction_New(&md, *method_capsule));
        QorePythonReferenceHolder meth(PyInstanceMethod_New(*func));
        const QoreMethod* m = reinterpret_cast<const QoreMethod*>(PyCapsule_GetPointer(*method_capsule, nullptr));
        PyDict_SetItemString(py_type.tp_dict, m->getName(), *meth);
    }
    py_static_meth_obj_vec.clear();

    py_type.qcls = &qcls;

    if (PyType_Ready(&py_type) < 0) {
        printd(5, "PythonQoreClass::PythonQoreClass() %s.%s type initialization failed\n", module_name, qcls.getName());
    }
}

void PythonQoreClass::populateClass(const QoreClass& qcls, cstrset_t& meth_set) {
    {
        QoreMethodIterator i(qcls);
        while (i.next()) {
            const QoreMethod* m = i.getMethod();
            if (m->getAccess() > Private) {
                continue;
            }
            cstrset_t::iterator mi = meth_set.lower_bound(m->getName());
            if (mi == meth_set.end() || strcmp(*mi, m->getName())) {
                meth_set.insert(mi, m->getName());
                QoreStringMaker mdoc("Python wrapper for Qore class method %s::%s()", qcls.getName(), m->getName());
                strvec.push_back(mdoc);

                py_normal_meth_vec.push_back({m->getName(), (PyCFunction)exec_qore_method, METH_VARARGS, strvec.back().c_str()});
                py_normal_meth_obj_vec.push_back(PyCapsule_New((void*)m, nullptr, nullptr));
            }
        }
    }

    {
        QoreStaticMethodIterator i(qcls);
        while (i.next()) {
            const QoreMethod* m = i.getMethod();
            if (m->getAccess() > Private) {
                continue;
            }
            cstrset_t::iterator mi = meth_set.lower_bound(m->getName());
            if (mi == meth_set.end() || strcmp(*mi, m->getName())) {
                meth_set.insert(mi, m->getName());
                QoreStringMaker mdoc("Python wrapper for Qore static class method %s::%s()", qcls.getName(), m->getName());
                strvec.push_back(mdoc);
                py_static_meth_vec.push_back({m->getName(), (PyCFunction)exec_qore_static_method, METH_VARARGS, strvec.back().c_str()});
                py_static_meth_obj_vec.push_back(PyCapsule_New((void*)m, nullptr, nullptr));
            }
        }
    }

    ExceptionSink xsink;
    {
        QoreClassConstantIterator i(qcls);
        while (i.next()) {
            const QoreExternalConstant& c = i.get();
            if (c.getAccess() > Private) {
                continue;
            }
            cstrset_t::iterator mi = meth_set.lower_bound(c.getName());
            if (mi == meth_set.end() || strcmp(*mi, c.getName())) {
                meth_set.insert(mi, c.getName());
                ValueHolder qoreval(c.getReferencedValue(), &xsink);
                QorePythonProgram* qore_python_pgm = QorePythonProgram::getContext();
                QorePythonReferenceHolder val(qore_python_pgm->getPythonValue(*qoreval, &xsink));
                if (!xsink) {
                    PyDict_SetItemString(py_type.tp_dict, c.getName(), *val);
                }
            }
        }
    }
}

PyObject* PythonQoreClass::wrap(QoreObject* obj) {
    PyQoreObject* self = (PyQoreObject*)py_type.tp_alloc(&py_type, 0);
    obj->tRef();
    self->qobj = obj;
    // save a strong reference to the Qore object
    ExceptionSink xsink;
    obj->ref();
    QorePythonProgram* qore_python_pgm = QorePythonProgram::getContext();
    qore_python_pgm->saveQoreObjectFromPython(obj, xsink);
    if (xsink) {
        qore_python_pgm->raisePythonException(xsink);
    }
    return (PyObject*)self;
}

PyObject* PythonQoreClass::exec_qore_method(PyObject* method_capsule, PyObject* args) {
    QorePythonReferenceHolder argstr(PyObject_Repr(args));
    assert(PyUnicode_Check(*argstr));
    // get method
    const QoreMethod* m = reinterpret_cast<const QoreMethod*>(PyCapsule_GetPointer(method_capsule, nullptr));
    assert(PyTuple_Check(args));
    QoreObject* obj;
    // check if this could be a static method call
    if (!PyTuple_Size(args)) {
        obj = nullptr;
    } else {
        // returns a borrowed reference
        PyObject* py_obj = PyTuple_GetItem(args, 0);
        if (!PyQoreObject_Check(py_obj)) {
            obj = nullptr;
        } else {
            obj = reinterpret_cast<PyQoreObject*>(py_obj)->qobj;
            // if the class is not accessible, do not make the call
            if (obj->getClassAccess(*m->getClass()) > Private) {
                obj = nullptr;
            }
        }
    }
    printd(5, "PythonQoreClass::exec_qore_method() %s::%s() obj: %p (%s) args: %s\n", m->getClassName(), m->getName(), obj, obj ? obj->getClassName() : "n/a", PyUnicode_AsUTF8(*argstr));
    if (!obj) {
        // see if a static method with the same name is available
        ClassAccess access;
        const QoreMethod* static_meth = m->getClass()->findStaticMethod(m->getName(), access);
        if (!static_meth) {
            QoreStringMaker desc("cannot call normal method '%s::%s()' without a 'self' object argument that " \
                "inherits '%s'", m->getClassName(), m->getName(), m->getClassName());
            PyErr_SetString((PyObject*)&PythonQoreException_Type, desc.c_str());
            return nullptr;
        }
        return exec_qore_static_method(*static_meth, args);
    }

    QorePythonProgram* qore_python_pgm = QorePythonProgram::getContext();
    ExceptionSink xsink;
    QoreExternalProgramContextHelper pch(&xsink, qore_python_pgm->getQoreProgram());

    ReferenceHolder<QoreListNode> qargs(qore_python_pgm->getQoreListFromTuple(&xsink, args, 1), &xsink);
    if (!xsink) {
        ValueHolder rv(obj->evalMethod(*m, *qargs, &xsink), &xsink);
        QorePythonReferenceHolder py_rv(qore_python_pgm->getPythonValue(*rv, &xsink));
        if (!xsink) {
            return py_rv.release();
        }
    }

    qore_python_pgm->raisePythonException(xsink);
    return nullptr;
}

PyObject* PythonQoreClass::exec_qore_static_method(PyObject* method_capsule, PyObject* args) {
    QorePythonReferenceHolder argstr(PyObject_Repr(args));
    assert(PyUnicode_Check(*argstr));
    // get method
    const QoreMethod* m = reinterpret_cast<const QoreMethod*>(PyCapsule_GetPointer(method_capsule, nullptr));
    assert(PyTuple_Check(args));
    printd(5, "PythonQoreClass::exec_qore_static_method() %s::%s() args: %s\n", m->getClassName(), m->getName(), PyUnicode_AsUTF8(*argstr));

    return exec_qore_static_method(*m, args);
}

PyObject* PythonQoreClass::exec_qore_static_method(const QoreMethod& m, PyObject* args) {
    QorePythonProgram* qore_python_pgm = QorePythonProgram::getContext();
    ExceptionSink xsink;
    QoreExternalProgramContextHelper pch(&xsink, qore_python_pgm->getQoreProgram());

    ReferenceHolder<QoreListNode> qargs(qore_python_pgm->getQoreListFromTuple(&xsink, args, 1), &xsink);
    if (!xsink) {
        ValueHolder rv(QoreObject::evalStaticMethod(m, m.getClass(), *qargs, &xsink), &xsink);
        QorePythonReferenceHolder py_rv(qore_python_pgm->getPythonValue(*rv, &xsink));
        if (!xsink) {
            return py_rv.release();
        }
    }

    qore_python_pgm->raisePythonException(xsink);
    return nullptr;
}

PyObject* PythonQoreClass::py_new(QorePyTypeObject* type, PyObject* args, PyObject* kw) {
    QorePythonReferenceHolder argstr(PyObject_Repr(args));
    assert(PyUnicode_Check(*argstr));
    printd(5, "PythonQoreClass::py_new() type: %s (qore: %s) args: %s kw: %p\n", type->tp_name, type->qcls->getName(), PyUnicode_AsUTF8(*argstr), kw);
    assert(PyTuple_Check(args));
    QorePythonProgram* qore_python_pgm = QorePythonProgram::getContext();
    ExceptionSink xsink;
    ReferenceHolder<QoreListNode> qargs(qore_python_pgm->getQoreListFromTuple(&xsink, args), &xsink);
    if (!xsink) {
        QoreExternalProgramContextHelper pch(&xsink, qore_python_pgm->getQoreProgram());

        ReferenceHolder<QoreObject> qobj(type->qcls->execConstructor(*qargs, &xsink), &xsink);
        if (!xsink) {
            PyQoreObject* self = (PyQoreObject*)type->tp_alloc(type, 0);
            qobj->tRef();
            self->qobj = *qobj;
            // save a strong reference to the Qore object
            qore_python_pgm->saveQoreObjectFromPython(qobj.release(), xsink);
            return (PyObject*)self;
        }
    }

    qore_python_pgm->raisePythonException(xsink);
    return nullptr;
}

void PythonQoreClass::py_dealloc(PyQoreObject* self) {
    if (self->qobj) {
        self->qobj->tDeref();
        self->qobj = nullptr;
    }
    Py_TYPE(self)->tp_free(self);
}

PyObject* PythonQoreClass::py_repr(PyObject* obj) {
    QoreStringMaker str("Qore %s object %p", Py_TYPE(obj)->tp_name, obj);
    return PyUnicode_FromKindAndData(PyUnicode_1BYTE_KIND, str.c_str(), str.size());
}