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
#include <frameobject.h>
#include <datetime.h>

void QorePythonProgram::staticInit() {
    PyDateTime_IMPORT;
}

QoreListNode* QorePythonProgram::getQoreListFromList(PyObject* val, ExceptionSink* xsink) {
    assert(PyList_Check(val));
    ReferenceHolder<QoreListNode> rv(new QoreListNode(autoTypeInfo), xsink);
    Py_ssize_t len = PyList_Size(val);
    for (Py_ssize_t i = 0; i < len; ++i) {
        ValueHolder qval(getQoreValue(PyList_GetItem(val, i), xsink), xsink);
        if (*xsink) {
            return nullptr;
        }
        rv->push(qval.release(), xsink);
        assert(!*xsink);
    }
    return rv.release();
}

QoreListNode* QorePythonProgram::getQoreListFromTuple(PyObject* val, ExceptionSink* xsink) {
    assert(PyTuple_Check(val));
    ReferenceHolder<QoreListNode> rv(new QoreListNode(autoTypeInfo), xsink);
    Py_ssize_t len = PyTuple_Size(val);
    for (Py_ssize_t i = 0; i < len; ++i) {
        ValueHolder qval(getQoreValue(PyTuple_GetItem(val, i), xsink), xsink);
        if (*xsink) {
            return nullptr;
        }
        rv->push(qval.release(), xsink);
        assert(!*xsink);
    }
    return rv.release();
}

QoreHashNode* QorePythonProgram::getQoreHashFromDict(PyObject* val, ExceptionSink* xsink) {
    assert(PyDict_Check(val));
    ReferenceHolder<QoreHashNode> rv(new QoreHashNode(autoTypeInfo), xsink);

    PyObject* key, * value;
    Py_ssize_t pos = 0;
    while (PyDict_Next(val, &pos, &key, &value)) {
        const char* keystr;
        QorePythonReferenceHolder tkey;
        if (Py_TYPE(key) == &PyUnicode_Type) {
            Py_ssize_t size;
            keystr = PyUnicode_AsUTF8AndSize(key, &size);
        } else {
            tkey = PyObject_Repr(key);
            assert(Py_TYPE(*tkey) == &PyUnicode_Type);
            Py_ssize_t size;
            keystr = PyUnicode_AsUTF8AndSize(*tkey, &size);
        }

        ValueHolder qval(getQoreValue(value, xsink), xsink);
        if (*xsink) {
            return nullptr;
        }
        rv->setKeyValue(keystr, qval.release(), xsink);
    }
    return rv.release();
}

BinaryNode* QorePythonProgram::getQoreBinaryFromBytes(PyObject* val, ExceptionSink* xsink) {
    assert(PyBytes_Check(val));
    SimpleRefHolder<BinaryNode> rv(new BinaryNode);
    rv->append(PyBytes_AS_STRING(val), PyBytes_GET_SIZE(val));
    return rv.release();
}

BinaryNode* QorePythonProgram::getQoreBinaryFromByteArray(PyObject* val, ExceptionSink* xsink) {
    assert(PyByteArray_Check(val));
    SimpleRefHolder<BinaryNode> rv(new BinaryNode);
    rv->append(PyByteArray_AsString(val), PyByteArray_Size(val));
    return rv.release();
}

DateTimeNode* QorePythonProgram::getQoreDateTimeFromDelta(PyObject* val, ExceptionSink* xsink) {
    assert(PyDelta_Check(val));
    return DateTimeNode::makeRelative(0, 0, PyDateTime_DELTA_GET_DAYS(val), 0, 0, PyDateTime_DELTA_GET_SECONDS(val),
        PyDateTime_DELTA_GET_MICROSECONDS(val));
}

DateTimeNode* QorePythonProgram::getQoreDateTimeFromDateTime(PyObject* val, ExceptionSink* xsink) {
    assert(PyDateTime_Check(val));

    const AbstractQoreZoneInfo* zone = nullptr;
    if (PyObject_HasAttrString(val, "tzinfo")) {
        // get UTC offset for time
        QorePythonReferenceHolder tzinfo(PyObject_GetAttrString(val, "tzinfo"));
        if (tzinfo && PyTZInfo_Check(*tzinfo)) {
            assert(PyObject_HasAttrString(*tzinfo, "utcoffset"));
            QorePythonReferenceHolder utcoffset_func(PyObject_GetAttrString(*tzinfo, "utcoffset"));
            assert(PyCallable_Check(*utcoffset_func));
            QorePythonReferenceHolder args(PyTuple_New(1));
            Py_INCREF(val);
            PyTuple_SET_ITEM(*args, 0, val);

            QorePythonReferenceHolder delta(PyEval_CallObject(*utcoffset_func, *args));
            if (delta && PyDelta_Check(*delta)) {
                zone = findCreateOffsetZone(PyDateTime_DELTA_GET_SECONDS(*delta));
                //printd(5, "TZ RV: %p '%s' utcoffset: %d (%p)\n", *delta, Py_TYPE(*delta)->tp_name, PyDateTime_DELTA_GET_SECONDS(*delta), zone);
            }
        }
    }
    return DateTimeNode::makeAbsolute(zone ? zone : currentTZ(), PyDateTime_GET_YEAR(val), PyDateTime_GET_MONTH(val),
        PyDateTime_GET_DAY(val), PyDateTime_DATE_GET_HOUR(val), PyDateTime_DATE_GET_MINUTE(val),
        PyDateTime_DATE_GET_SECOND(val), PyDateTime_DATE_GET_MICROSECOND(val));
}

DateTimeNode* QorePythonProgram::getQoreDateTimeFromDate(PyObject* val, ExceptionSink* xsink) {
    assert(PyDate_Check(val));
    return DateTimeNode::makeAbsolute(currentTZ(), PyDateTime_GET_YEAR(val), PyDateTime_GET_MONTH(val),
        PyDateTime_GET_DAY(val), 0, 0, 0, 0);
}

DateTimeNode* QorePythonProgram::getQoreDateTimeFromTime(PyObject* val, ExceptionSink* xsink) {
    assert(PyDateTime_Check(val));
    return DateTimeNode::makeAbsolute(currentTZ(), 0, 0, 0, PyDateTime_TIME_GET_HOUR(val),
        PyDateTime_TIME_GET_MINUTE(val), PyDateTime_TIME_GET_SECOND(val), PyDateTime_TIME_GET_MICROSECOND(val));
}

QoreValue QorePythonProgram::getQoreValue(QorePythonReferenceHolder& val, ExceptionSink* xsink) {
    return getQoreValue(*val, xsink);
}

QoreValue QorePythonProgram::getQoreValue(PyObject* val, ExceptionSink* xsink) {
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

PyObject* QorePythonProgram::getPythonList(ExceptionSink* xsink, const QoreListNode* l) {
    QorePythonReferenceHolder list(PyList_New(l->size()));
    ConstListIterator i(l);
    while (i.next()) {
        QorePythonReferenceHolder val(getPythonValue(i.getValue(), xsink));
        if (*xsink) {
            return nullptr;
        }
        PyList_SetItem(*list, i.index(), val.release());
    }

    return list.release();
}

PyObject* QorePythonProgram::getPythonTupleValue(ExceptionSink* xsink, const QoreListNode* l, size_t arg_offset, PyObject* first) {
    if (!l || (l->size() < arg_offset)) {
        return nullptr;
    }

    Py_ssize_t size = l->size() - arg_offset;
    if (first) {
        ++size;
    }
    QorePythonReferenceHolder tuple(PyTuple_New(size));
    size_t offset = 0;
    if (first) {
        Py_INCREF(first);
        PyTuple_SET_ITEM(*tuple, 0, first);
        offset = 1;
    }
    ConstListIterator i(l, arg_offset - 1);
    while (i.next()) {
        QorePythonReferenceHolder val(getPythonValue(i.getValue(), xsink));
        if (*xsink) {
            return nullptr;
        }
        PyTuple_SET_ITEM(*tuple, i.index() - arg_offset + offset, val.release());
    }

    return tuple.release();
}

PyObject* QorePythonProgram::getPythonDict(ExceptionSink* xsink, const QoreHashNode* h) {
    QorePythonReferenceHolder dict(PyDict_New());
    ConstHashIterator i(h);
    while (i.next()) {
        QorePythonReferenceHolder key(getPythonString(xsink, i.getKeyString()));
        if (*xsink) {
            return nullptr;
        }
        QorePythonReferenceHolder val(getPythonValue(i.get(), xsink));
        if (*xsink) {
            return nullptr;
        }
        PyDict_SetItem(*dict, *key, *val);
    }

    return dict.release();
}

PyObject* QorePythonProgram::getPythonString(ExceptionSink* xsink, const QoreString* str) {
    TempEncodingHelper py_str(str, QCS_UTF8, xsink);
    if (*xsink) {
        return nullptr;
    }
    return PyUnicode_FromKindAndData(PyUnicode_1BYTE_KIND, py_str->c_str(), py_str->size());
}

PyObject* QorePythonProgram::getPythonByteArray(ExceptionSink* xsink, const BinaryNode* b) {
    return PyByteArray_FromStringAndSize(reinterpret_cast<const char*>(b->getPtr()), b->size());
}

PyObject* QorePythonProgram::getPythonDelta(ExceptionSink* xsink, const DateTime* dt) {
    assert(dt->isRelative());

    // WARNING: years are converted to 365 days; months are converted to 30 days
    return PyDelta_FromDSU(dt->getYear() * 365 + dt->getMonth() * 30 + dt->getDay(),
        dt->getHour() * 3600 + dt->getMinute() * 60 + dt->getSecond(), dt->getMicrosecond());
}

PyObject* QorePythonProgram::getPythonDateTime(ExceptionSink* xsink, const DateTime* dt) {
    assert(dt->isAbsolute());

    return PyDateTime_FromDateAndTime(dt->getYear(), dt->getMonth(), dt->getDay(), dt->getHour(), dt->getMinute(),
        dt->getSecond(), dt->getMicrosecond());
}

PyObject* QorePythonProgram::getPythonValue(QoreValue val, ExceptionSink* xsink) {
    //printd(5, "QorePythonProgram::getPythonValue() type '%s'\n", val.getFullTypeName());
    switch (val.getType()) {
        case NT_NOTHING:
        case NT_NULL:
            Py_INCREF(Py_None);
            return Py_None;

        case NT_BOOLEAN: {
            PyObject* rv = val.getAsBool() ? Py_True : Py_False;
            Py_INCREF(rv);
            return rv;
        }

        case NT_INT:
            return PyLong_FromLongLong(val.getAsBigInt());

        case NT_FLOAT:
            return PyFloat_FromDouble(val.getAsFloat());

        case NT_STRING:
            return getPythonString(xsink, val.get<const QoreStringNode>());

        case NT_LIST:
            return getPythonList(xsink, val.get<const QoreListNode>());

        case NT_HASH:
            return getPythonDict(xsink, val.get<const QoreHashNode>());

        case NT_BINARY:
            return getPythonByteArray(xsink, val.get<const BinaryNode>());

        case NT_DATE: {
            const DateTimeNode* dt = val.get<const DateTimeNode>();
            return dt->isRelative()
                ? getPythonDelta(xsink, dt)
                : getPythonDateTime(xsink, dt);
        }
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
    if (!py_func || !PyFunction_Check(py_func)) {
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

    return getQoreValue(return_value, xsink);
}

QoreValue QorePythonProgram::callMethod(ExceptionSink* xsink, const QoreString& class_name, const QoreString& method_name, const QoreListNode* args, size_t arg_offset) {
    TempEncodingHelper cname(class_name, QCS_UTF8, xsink);
    if (*xsink) {
        xsink->appendLastDescription(" (while processing the \"class_name\" argument)");
        return QoreValue();
    }

    TempEncodingHelper mname(method_name, QCS_UTF8, xsink);
    if (*xsink) {
        xsink->appendLastDescription(" (while processing the \"method_name\" argument)");
        return QoreValue();
    }

    return callMethod(xsink, cname->c_str(), mname->c_str(), args, arg_offset);
}

QoreValue QorePythonProgram::callMethod(ExceptionSink* xsink, const char* cname, const char* mname,
    const QoreListNode* args, size_t arg_offset, PyObject* first) {
    // returns a borrowed reference
    PyObject* py_class = PyDict_GetItemString(module_dict, cname);
    if (!py_class || !PyType_Check(py_class)) {
        py_class = PyDict_GetItemString(builtin_dict, cname);
        if (!py_class || !PyType_Check(py_class)) {
            xsink->raiseException("NO-CLASS", "cannot find class '%s'", cname);
            return QoreValue();
        }
    }

    // returns a borrowed reference
    QorePythonReferenceHolder py_method;
    if (PyObject_HasAttrString(py_class, mname)) {
        py_method = PyObject_GetAttrString(py_class, mname);
    }
    if (!py_method || (!PyFunction_Check(*py_method) && (Py_TYPE(*py_method) != &PyMethodDescr_Type))) {
        xsink->raiseException("NO-METHOD", "cannot find method '%s.%s()'", cname, mname);
        return QoreValue();
    }

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
        return_value = PyEval_CallObject(*py_method, *py_args);

        // check for Python exceptions
        if (!return_value && checkPythonException(xsink)) {
            return QoreValue();
        }
    }

    return getQoreValue(return_value, xsink);
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

    // get location
    QoreExternalProgramLocationWrapper loc;
    QoreCallStack callstack;

    printd(5, "QorePythonProgram::checkPythonException() type: %s val: %s (%p) traceback: %s\n",
        Py_TYPE(*ex_type)->tp_name, ex_value ? Py_TYPE(*ex_value)->tp_name : "(null)", *ex_value,
        traceback ? Py_TYPE(*traceback)->tp_name : "(null)");

    if (!ex_value) {
        ex_value = Py_None;
        Py_INCREF(Py_None);
    }
    if (!traceback) {
        traceback = Py_None;
        Py_INCREF(Py_None);
    }

    PyErr_NormalizeException(ex_type.getRef(), ex_value.getRef(), traceback.getRef());
    printd(5, "QorePythonProgram::checkPythonException() type: %s val: %s (%p) traceback: %s\n",
        Py_TYPE(*ex_type)->tp_name, Py_TYPE(*ex_value)->tp_name, *ex_value, Py_TYPE(*traceback)->tp_name);

    bool use_loc;
    if (PyTraceBack_Check(*traceback)) {
        PyTracebackObject* tb = reinterpret_cast<PyTracebackObject*>(*traceback);
        PyFrameObject* frame = tb->tb_frame;
        while (frame) {
            int line = PyCode_Addr2Line(frame->f_code, frame->f_lasti);
            const char* filename = getCString(frame->f_code->co_filename);
            const char* funcname = getCString(frame->f_code->co_name);
            if (frame == tb->tb_frame) {
                loc.set(filename, line, line, nullptr, 0, "Python");
            } else {
                callstack.add(CT_USER, filename, line, line, nullptr, "Python");
            }
            frame = frame->f_back;
        }
        use_loc = true;
    } else {
        use_loc = false;
    }

    // get description
    QorePythonReferenceHolder desc(PyObject_Str(*ex_value));
    ValueHolder qore_desc(getQoreValue(*desc, xsink), xsink);
    //ValueHolder arg(getQoreValue(*ex_value, xsink), xsink);
    ValueHolder arg(xsink);
    if (!*xsink) {
        if (use_loc) {
            xsink->raiseExceptionArg(loc.get(), Py_TYPE(*ex_value)->tp_name, arg.release(),
                qore_desc->getType() == NT_STRING ? qore_desc.release().get<QoreStringNode>() : nullptr, callstack);
        } else {
            xsink->raiseExceptionArg(Py_TYPE(*ex_value)->tp_name, arg.release(),
                qore_desc->getType() == NT_STRING ? qore_desc.release().get<QoreStringNode>() : nullptr, callstack);
        }
    } else {
        xsink->appendLastDescription(" (while trying to convert Python exception arguments to Qore)");
    }

    ex_type = nullptr;
    ex_value = nullptr;
    traceback = nullptr;

    return -1;
}