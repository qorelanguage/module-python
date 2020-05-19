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

    xsink->raiseException("PYTHON-VALUE-ERROR", "don't know how to convert a value of Python type '%s' to Qore",
        type->tp_name);
    return QoreValue();
}
