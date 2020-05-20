/* -*- mode: c++; indent-tabs-mode: nil -*- */
/*
    QorePythonExternalProgramData.cpp

    Qore Programming Language python Module

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

#include "QorePythonExternalProgramData.h"
#include "QorePythonPrivateData.h"
#include "QorePythonClass.h"

#include <datetime.h>

void QorePythonExternalProgramData::staticInit() {
    PyDateTime_IMPORT;
}

QoreValue QorePythonExternalProgramData::getQoreValue(PyObject* val, ExceptionSink* xsink) {
    //printd(5, "QorePythonBase::getQoreValue() val: %p\n", val);
    if (!val || val == Py_None) {
        return QoreValue();
    }

    PyTypeObject* type = Py_TYPE(val);
    if (type == &PyBool_Type) {
        return QoreValue(val == Py_True);
    }

    if (type == &PyLong_Type) {
        return QoreValue(PyLong_AsLong(val));
    }

    if (type == &PyFloat_Type) {
        return QoreValue(PyFloat_AS_DOUBLE(val));
    }

    if (type == &PyUnicode_Type) {
        Py_ssize_t size;
        const char* str = PyUnicode_AsUTF8AndSize(val, &size);
        return new QoreStringNode(str, size, QCS_UTF8);
    }

    if (type == &PyList_Type) {
        return getQoreListFromList(val, xsink);
    }

    if (type == &PyTuple_Type) {
        return getQoreListFromTuple(val, xsink);
    }

    if (type == &PyBytes_Type) {
        return getQoreBinaryFromBytes(val, xsink);
    }

    if (type == &PyByteArray_Type) {
        return getQoreBinaryFromByteArray(val, xsink);
    }

    if (type == PyDateTimeAPI->DateType) {
        return getQoreDateTimeFromDate(val, xsink);
    }

    if (type == PyDateTimeAPI->TimeType) {
        return getQoreDateTimeFromTime(val, xsink);
    }

    if (type == PyDateTimeAPI->DateTimeType) {
        return getQoreDateTimeFromDateTime(val, xsink);
    }

    if (type == PyDateTimeAPI->DeltaType) {
        return getQoreDateTimeFromDelta(val, xsink);
    }

    if (type == &PyDict_Type) {
        return getQoreHashFromDict(val, xsink);
    }

    QorePythonClass* cls = getCreateQorePythonClass(type, xsink);
    if (!cls) {
        assert(*xsink);
        return QoreValue();
    }

    Py_INCREF(val);
    return new QoreObject(cls, pgm, new QorePythonPrivateData(val));
}

QorePythonClass* QorePythonExternalProgramData::getCreateQorePythonClass(PyTypeObject* type, ExceptionSink* xsink) {
    // grab current Program's parse lock before manipulating namespaces
    CurrentProgramRuntimeExternalParseContextHelper pch;
    if (!pch) {
        xsink->raiseException("PROGRAM-ERROR", "cannot process Python type '%s' in deleted Program object",
            type->tp_name);
        return nullptr;
    }

    return getCreateQorePythonClassIntern(type, xsink);
}

QorePythonClass* QorePythonExternalProgramData::getCreateQorePythonClassIntern(PyTypeObject* type,
    ExceptionSink* xsink) {
    std::string name = type->tp_name;

    clmap_t::iterator i = clmap.find(name);
    if (i != clmap.end() && i->first == name) {
        return i->second;
    }

    // create new QorePythonClass
    size_t pos = name.find('.');
    assert(pos && (pos == std::string::npos || pos < (name.size() - 1)));
    QoreNamespace* ns;
    const char* cname;
    if (pos == std::string::npos) {
        cname = name.c_str();
        ns = pyns;
    } else {
        cname = name.c_str() + pos + 1;
        std::string module = name.substr(0, pos - 1);
        ns = pyns->findCreateNamespacePath(module.c_str());
    }

    // create new class
    std::unique_ptr<QorePythonClass> cls(new QorePythonClass(cname));

    // iterate base classes
    for (Py_ssize_t i = 0, end = PyTuple_GET_SIZE(type->tp_bases); i < end; ++i) {
        PyTypeObject* b = reinterpret_cast<PyTypeObject*>(PyTuple_GET_ITEM(type->tp_bases, i));
        assert(PyType_Check(b));

        QorePythonClass* bclass = getCreateQorePythonClassIntern(b, xsink);
        if (!bclass) {
            assert(*xsink);
            return nullptr;
        }

        printd(5, "QorePythonExternalProgramData::getCreateQorePythonClassIntern() %s parent: %s (bclass: %p)\n",
            name.c_str(), b->tp_name, bclass);
        cls->addBuiltinVirtualBaseClass(bclass);
    }

    printd(5, "QorePythonExternalProgramData::getCreateQorePythonClassIntern() %s methods: %p\n", name.c_str(),
        type->tp_methods);

    // process builtin methods
    if (type->tp_methods) {
        for (PyMethodDef* meth = type->tp_methods; meth->ml_name; ++meth) {
            assert(!((meth->ml_flags & METH_CLASS) && (meth->ml_flags & METH_STATIC)));
            bool is_static = meth->ml_flags & METH_STATIC;

            printd(5, "QorePythonExternalProgramData::getCreateQorePythonClassIntern() adding builtin method " \
                "%s.%s() meth: %p\n", name.c_str(), meth->ml_name, meth);
            assert(PyCallable_Check((PyObject*)meth));

            type_vec_t param_types;
            if (is_static) {
                cls->addStaticMethod((void*)meth, meth->ml_name,
                    (q_external_static_method_t)QorePythonExternalProgramData::execPythonStaticCMethod,
                    Public, QCF_USES_EXTRA_ARGS, QDOM_UNCONTROLLED_API, autoTypeInfo);
            } else {
                cls->addMethod((void*)meth, meth->ml_name,
                    (q_external_method_t)QorePythonExternalProgramData::execPythonNormalCMethod,
                    Public, QCF_USES_EXTRA_ARGS, QDOM_UNCONTROLLED_API, autoTypeInfo);
            }
        }
    }

    // process user methods
    if (type->tp_dict) {
        PyObject* key, * value;
        Py_ssize_t pos = 0;

        while (PyDict_Next(type->tp_dict, &pos, &key, &value)) {
            assert(Py_TYPE(key) == &PyUnicode_Type);
            const char* keystr;
            Py_ssize_t size;
            keystr = PyUnicode_AsUTF8AndSize(key, &size);

            //printd(5, "%s: %s: %s\n", name.c_str(), keystr, Py_TYPE(value)->tp_name);

            PyTypeObject* meth_type = Py_TYPE(value);
            if (meth_type == &PyStaticMethod_Type) {
                // get callable from static method
                PyObject* py_method = PyStaticMethod_Type.tp_descr_get(value, nullptr, nullptr);
                assert(py_method);
                cls->addObj(py_method);
                cls->addStaticMethod((void*)py_method, keystr,
                    (q_external_static_method_t)QorePythonExternalProgramData::execPythonStaticMethod,
                    Public, QCF_USES_EXTRA_ARGS, QDOM_UNCONTROLLED_API, autoTypeInfo);
                printd(5, "QorePythonExternalProgramData::getCreateQorePythonClassIntern() added static method " \
                    "%s.%s() (%s)\n", name.c_str(), keystr, Py_TYPE(value)->tp_name);
                continue;
            }
            if (PyFunction_Check(value)) {
                cls->addMethod((void*)value, keystr,
                    (q_external_method_t)QorePythonExternalProgramData::execPythonNormalMethod, Public,
                    QCF_USES_EXTRA_ARGS, QDOM_UNCONTROLLED_API, autoTypeInfo);
                printd(5, "QorePythonExternalProgramData::getCreateQorePythonClassIntern() added normal method " \
                    "%s.%s() (%s)\n", name.c_str(), keystr, Py_TYPE(value)->tp_name);
                continue;
            }
        }
    }

    /*
    struct PyMemberDef *tp_members;
    PyObject *tp_bases;
    */

    /*
    QorePythonReferenceHolder py_method;
    if (PyObject_HasAttrString(type, mname)) {
        py_method = PyObject_GetAttrString(py_class, mname);
    }
    */

    //xsink->raiseException("ERR", "%s", name.c_str());
    //return nullptr;

    return cls.release();
}

QoreValue QorePythonExternalProgramData::execPythonStaticCMethod(const QoreMethod& meth, PyMethodDef* m,
    const QoreListNode* args, q_rt_flags_t rtflags, ExceptionSink* xsink) {
    QorePythonExternalProgramData* pypd = QorePythonExternalProgramData::getContext();
    assert(pypd);
    return pypd->callCMethod(xsink, m, args);
}

QoreValue QorePythonExternalProgramData::execPythonNormalCMethod(const QoreMethod& meth, PyMethodDef* m,
    QoreObject* self, QorePythonPrivateData* pd, const QoreListNode* args, q_rt_flags_t rtflags,
    ExceptionSink* xsink) {
    QoreProgram* pgm = self->getProgram();
    assert(pgm);
    QorePythonExternalProgramData* pypd = static_cast<QorePythonExternalProgramData*>(pgm->getExternalData("python"));
    assert(pypd);

    return pypd->callCMethod(xsink, m, args, 0, pd->get());
}

QoreValue QorePythonExternalProgramData::callCMethod(ExceptionSink* xsink, PyMethodDef* meth,
    const QoreListNode* args, size_t arg_offset, PyObject* first) {
    QorePythonReferenceHolder py_args;
    if (args && args->size() > arg_offset) {
        py_args = getPythonTupleValue(xsink, args, arg_offset, first);
        if (*xsink) {
            return QoreValue();
        }
    }

    QorePythonReferenceHolder return_value;
    {
        //printd(5, "QorePythonProgram::callFunction(): calling '%s' argcount: %d\n", fname->c_str(), (args && args->size() > arg_offset) ? args->size() - arg_offset : 0);
        QorePythonHelper qph(python);
        //return_value = PyCFunction_Call((PyObject*)meth->ml_meth, *py_args, nullptr);
        return_value = _PyMethodDef_RawFastCallDict(meth, first, py_args.getRef(),
            (args && args->size() > arg_offset) ? args->size() - arg_offset : 0, nullptr);

        // check for Python exceptions
        if (!return_value && checkPythonException(xsink)) {
            return QoreValue();
        }
    }

    return getQoreValue(*return_value, xsink);
}

QoreValue QorePythonExternalProgramData::execPythonStaticMethod(const QoreMethod& meth, PyObject* m, const QoreListNode* args,
    q_rt_flags_t rtflags, ExceptionSink* xsink) {
    QorePythonExternalProgramData* pypd = QorePythonExternalProgramData::getContext();
    assert(pypd);
    return pypd->callInternal(xsink, m, args);
}

QoreValue QorePythonExternalProgramData::execPythonNormalMethod(const QoreMethod& meth, PyObject* m, QoreObject* self,
    QorePythonPrivateData* pd, const QoreListNode* args, q_rt_flags_t rtflags, ExceptionSink* xsink) {
    QoreProgram* pgm = self->getProgram();
    assert(pgm);
    QorePythonExternalProgramData* pypd = static_cast<QorePythonExternalProgramData*>(pgm->getExternalData("python"));
    assert(pypd);

    return pypd->callInternal(xsink, m, args, 0, pd->get());
}
