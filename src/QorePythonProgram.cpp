/* -*- mode: c++; indent-tabs-mode: nil -*- */
/** @file QorePythonProgram.cpp defines the QorePythonProgram class */
/*
    QorePythonProgram.qpp

    Qore Programming Language

    Copyright 2020 Qore Technologies, s.r.o.

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

#include "QC_PythonProgram.h"

QoreValue QorePythonProgram::getQoreValue(PyObject* val, ExceptionSink* xsink) {
    //printd(0, "QorePythonBase::getQoreValue() val: %p\n", val);
    QorePythonReferenceHolder holder(val);
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

    xsink->raiseException("PYTHON-VALUE-ERROR", "don't know how to convert a value of Python type '%s' to Qore",
        type->tp_name);
    return QoreValue();
}

PyObject* QorePythonProgram::getPythonListValue(ExceptionSink* xsink, const QoreListNode* l, size_t arg_offset) {
    if (!l || (l->size() < arg_offset)) {
        return nullptr;
    }

    QorePythonReferenceHolder list(PyList_New(l->size()));
    ConstListIterator i(l, arg_offset);
    while (i.next()) {
        QorePythonReferenceHolder val(getPythonValue(i.getValue(), xsink));
        if (*xsink) {
            return nullptr;
        }
        PyList_SetItem(*list, i.index(), val.release());
    }

    return list.release();
}

PyObject* QorePythonProgram::getPythonTupleValue(ExceptionSink* xsink, const QoreListNode* l, size_t arg_offset) {
    if (!l || (l->size() < arg_offset)) {
        return nullptr;
    }

    QorePythonReferenceHolder tuple(PyTuple_New(l->size() - arg_offset));
    ConstListIterator i(l, arg_offset - 1);
    while (i.next()) {
        QorePythonReferenceHolder val(getPythonValue(i.getValue(), xsink));
        if (*xsink) {
            return nullptr;
        }
        PyTuple_SET_ITEM(*tuple, i.index() - arg_offset, val.release());
    }

    return tuple.release();
}

PyObject* QorePythonProgram::getPythonValue(QoreValue val, ExceptionSink* xsink) {
    //printd(5, "QorePythonProgram::getPythonValue() type '%s'\n", val.getFullTypeName());
    PyObject* rv = nullptr;
    switch (val.getType()) {
        case NT_NOTHING:
            rv = Py_None;
            break;

        case NT_BOOLEAN:
            rv = val.getAsBool() ? Py_True : Py_False;
            break;

        case NT_INT:
            rv = PyLong_FromLongLong(val.getAsBigInt());
            break;

        case NT_FLOAT:
            rv = PyFloat_FromDouble(val.getAsFloat());
            break;

        case NT_STRING: {
            TempEncodingHelper py_str(val.get<const QoreStringNode>(), QCS_UTF8, xsink);
            if (*xsink) {
                return nullptr;
            }
            rv = PyUnicode_FromKindAndData(PyUnicode_1BYTE_KIND, py_str->c_str(), py_str->size());
            break;
        }

        case NT_LIST:
            rv = getPythonListValue(xsink, val.get<const QoreListNode>());
            break;
    }

    if (rv) {
        Py_INCREF(rv);
        return rv;
    }

    xsink->raiseException("PYTHON-VALUE-ERROR", "don't know how to convert a value of Qore type '%s' to Python",
        val.getFullTypeName());
    return nullptr;
}

QoreValue QorePythonProgram::callFunction(ExceptionSink* xsink, const QoreString& func_name, const QoreListNode* args, size_t arg_offset) {
    TempEncodingHelper fname(func_name, QCS_UTF8, xsink);
    if (*xsink) {
        xsink->appendLastDescription(" (while processing the \"func_name\" argument)");
        return QoreValue();
    }

    // returns a borrowed reference
    PyObject* py_func = PyDict_GetItemString(module_dict, fname->c_str());
    if (!py_func) {
        xsink->raiseException("NO-FUNCTION", "cannot find function '%s'", fname->c_str());
        return QoreValue();
    }

    QorePythonReferenceHolder py_args;
    int argcount;
    if (args && args->size() > arg_offset) {
        py_args = getPythonTupleValue(xsink, args, arg_offset);
        if (*xsink) {
            return QoreValue();
        }
        argcount = args->size() - arg_offset;
    } else {
        argcount = 0;
    }

    QorePythonReferenceHolder return_value;
    {
        //printd(5, "QorePythonProgram::callFunction(): calling '%s' argcount: %d\n", fname->c_str(), argcount);
        QorePythonHelper qph(python);
        return_value = PyEval_CallObject(py_func, *py_args);

        // check for Python exceptions
        if (!return_value && checkPythonException(xsink)) {
            return QoreValue();
        }
    }

    return getQoreValue(return_value.release(), xsink);
}

int QorePythonProgram::checkPythonException(ExceptionSink* xsink) {
    // returns a borrowed reference
    PyObject* ex = PyErr_Occurred();
    if (!ex) {
        //printd(5, "QorePythonProgram::checkPythonException() no error\n");
        return 0;
    }

    QorePythonReferenceHolder ex_type, ex_value, traceback;
    PyErr_Fetch(ex_type.getRef(), ex_value.getRef(), traceback.getRef());
    assert(ex_type);
    assert(ex_value);
    //PyErr_NormalizeException(ex_type.getRef(), ex_value.getRef(), traceback.getRef());

    if (PyExceptionClass_Check(*ex_type) && PyUnicode_Check(*ex_value)) {
        Py_ssize_t size;
        const char* valstr = PyUnicode_AsUTF8AndSize(*ex_value, &size);

        //printd(0, "QorePythonProgram::checkPythonException() (%s) '%s' tb: %s\n", Py_TYPE(ex)->tp_name, valstr, traceback ? Py_TYPE(*traceback)->tp_name : "n/a");
        xsink->raiseException(PyExceptionClass_Name(*ex_type), "%s", valstr);
    } else {
        assert(false);
    }

    return -1;
}