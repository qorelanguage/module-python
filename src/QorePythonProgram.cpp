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
#include "QorePythonPrivateData.h"
#include "QorePythonClass.h"

#include <structmember.h>
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
            keystr = PyUnicode_AsUTF8(key);
        } else {
            tkey = PyObject_Repr(key);
            assert(Py_TYPE(*tkey) == &PyUnicode_Type);
            keystr = PyUnicode_AsUTF8(*tkey);
        }

        ValueHolder qval(getQoreValue(value, xsink), xsink);
        if (*xsink) {
            return nullptr;
        }
        rv->setKeyValue(keystr, qval.release(), xsink);
    }
    return rv.release();
}

BinaryNode* QorePythonProgram::getQoreBinaryFromBytes(PyObject* val) {
    assert(PyBytes_Check(val));
    SimpleRefHolder<BinaryNode> rv(new BinaryNode);
    rv->append(PyBytes_AS_STRING(val), PyBytes_GET_SIZE(val));
    return rv.release();
}

BinaryNode* QorePythonProgram::getQoreBinaryFromByteArray(PyObject* val) {
    assert(PyByteArray_Check(val));
    SimpleRefHolder<BinaryNode> rv(new BinaryNode);
    rv->append(PyByteArray_AsString(val), PyByteArray_Size(val));
    return rv.release();
}

DateTimeNode* QorePythonProgram::getQoreDateTimeFromDelta(PyObject* val) {
    assert(PyDelta_Check(val));
    return DateTimeNode::makeRelative(0, 0, PyDateTime_DELTA_GET_DAYS(val), 0, 0, PyDateTime_DELTA_GET_SECONDS(val),
        PyDateTime_DELTA_GET_MICROSECONDS(val));
}

DateTimeNode* QorePythonProgram::getQoreDateTimeFromDateTime(PyObject* val) {
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

DateTimeNode* QorePythonProgram::getQoreDateTimeFromDate(PyObject* val) {
    assert(PyDate_Check(val));
    return DateTimeNode::makeAbsolute(currentTZ(), PyDateTime_GET_YEAR(val), PyDateTime_GET_MONTH(val),
        PyDateTime_GET_DAY(val), 0, 0, 0, 0);
}

DateTimeNode* QorePythonProgram::getQoreDateTimeFromTime(PyObject* val) {
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
        return getQoreBinaryFromBytes(val);
    }

    if (type == &PyByteArray_Type) {
        return getQoreBinaryFromByteArray(val);
    }

    if (type == PyDateTimeAPI->DateType) {
        return getQoreDateTimeFromDate(val);
    }

    if (type == PyDateTimeAPI->TimeType) {
        return getQoreDateTimeFromTime(val);
    }

    if (type == PyDateTimeAPI->DateTimeType) {
        return getQoreDateTimeFromDateTime(val);
    }

    if (type == PyDateTimeAPI->DeltaType) {
        return getQoreDateTimeFromDelta(val);
    }

    if (type == &PyDict_Type) {
        return getQoreHashFromDict(val, xsink);
    }

    QorePythonClass* cls = getCreateQorePythonClass(xsink, type);
    if (!cls) {
        assert(*xsink);
        return QoreValue();
    }

    Py_INCREF(val);
    return new QoreObject(cls, qpgm, new QorePythonPrivateData(val));
}

QoreValue QorePythonProgram::getQoreAttr(PyObject* obj, const char* attr, ExceptionSink* xsink) {
    QorePythonReferenceHolder return_value;
    {
        QorePythonHelper qph(python);
        return_value = PyObject_GetAttrString(obj, attr);

        // check for Python exceptions
        if (!return_value && checkPythonException(xsink)) {
            return QoreValue();
        }
    }

    return getQoreValue(*return_value, xsink);
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
    bool has_list = (l && l->size() >= arg_offset);

    if (!first && !has_list) {
        return nullptr;
    }

    Py_ssize_t size = has_list ? (l->size() - arg_offset) : 0;
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
    if (has_list) {
        ConstListIterator i(l, arg_offset - 1);
        while (i.next()) {
            QorePythonReferenceHolder val(getPythonValue(i.getValue(), xsink));
            if (*xsink) {
                return nullptr;
            }
            PyTuple_SET_ITEM(*tuple, i.index() - arg_offset + offset, val.release());
        }
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

    return callInternal(xsink, py_func, args, arg_offset);
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

    return callInternal(xsink, *py_method, args, arg_offset);
}

QoreValue QorePythonProgram::callInternal(ExceptionSink* xsink, PyObject* callable, const QoreListNode* args,
    size_t arg_offset, PyObject* first) {
    QorePythonReferenceHolder rv(callPythonInternal(xsink, callable, args, arg_offset, first));
    return *xsink ? QoreValue() : getQoreValue(rv.release(), xsink);
}

PyObject* QorePythonProgram::callPythonInternal(ExceptionSink* xsink, PyObject* callable, const QoreListNode* args,
    size_t arg_offset, PyObject* first) {
    QorePythonReferenceHolder py_args;
    if (first || (args && args->size() > arg_offset)) {
        py_args = getPythonTupleValue(xsink, args, arg_offset, first);
        if (*xsink) {
            return nullptr;
        }
    }

    QorePythonReferenceHolder return_value;
    {
        //printd(5, "QorePythonProgram::callPythonInternal(): this: %p valid: %d argcount: %d\n", this, valid, (args && args->size() > arg_offset) ? args->size() - arg_offset : 0);
        QorePythonHelper qph(python);
        if (checkValid(xsink)) {
            return nullptr;
        }

        return_value = PyEval_CallObject(callable, *py_args);

        // check for Python exceptions
        if (!return_value && checkPythonException(xsink)) {
            return nullptr;
        }
    }

    return return_value.release();
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
                loc.set(filename, line, line, nullptr, 0, QORE_PYTHON_LANG_NAME);
            } else {
                callstack.add(CT_USER, filename, line, line, nullptr, QORE_PYTHON_LANG_NAME);
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

QoreValue QorePythonProgram::callPythonMethod(ExceptionSink* xsink, PyObject* attr, PyObject* obj,
    const QoreListNode* args, size_t arg_offset) {
    PyTypeObject* mtype = Py_TYPE(attr);
    printf("callPythonMethod() '%s' '%s'\n", mtype->tp_name, Py_TYPE(obj)->tp_name);
    // check for static method
    if (mtype == &PyStaticMethod_Type) {
        // get callable from static method
        QorePythonReferenceHolder py_method(PyStaticMethod_Type.tp_descr_get(attr, nullptr, nullptr));
        assert(py_method);
        return callInternal(xsink, *py_method, args, arg_offset);
    }
    // check for wrapper descriptors -> normal method
    if (mtype == &PyWrapperDescr_Type) {
        return callWrapperDescriptorMethod(xsink, obj, attr, args, arg_offset);
    }
    if (mtype == &PyMethodDescr_Type) {
        return callMethodDescriptorMethod(xsink, obj, attr, args, arg_offset);
    }
    if (mtype == &PyClassMethodDescr_Type) {
        return callClassMethodDescriptorMethod(xsink, obj, attr, args, arg_offset);
    }
    if (PyFunction_Check(attr)) {
        return callInternal(xsink, attr, args, arg_offset, obj);
    }
    if (PyCFunction_Check(attr)) {
        return callCFunctionMethod(xsink, attr, args, arg_offset);
    }

    xsink->raiseException("PYTHON-ERROR", "cannot make a call with Python type '%s'", mtype->tp_name);
    return QoreValue();
}

QorePythonClass* QorePythonProgram::getCreateQorePythonClass(ExceptionSink* xsink, PyTypeObject* type) {
    // grab current Program's parse lock before manipulating namespaces
    CurrentProgramRuntimeExternalParseContextHelper pch;
    if (!pch) {
        xsink->raiseException("PROGRAM-ERROR", "cannot process Python type '%s' in deleted Program object",
            type->tp_name);
        return nullptr;
    }

    return getCreateQorePythonClassIntern(xsink, type);
}

QorePythonClass* QorePythonProgram::getCreateQorePythonClassIntern(ExceptionSink* xsink, PyTypeObject* type,
    QoreNamespace* parent_ns) {
    // absolute class name
    std::string cpath = type->tp_name;
    if (module_context) {
        cpath = module_context;
        cpath += '.';
        cpath += type->tp_name;
    }

    //printd(5, "QorePythonProgram::getCreateQorePythonClassIntern() class: '%s'\n", cpath.c_str());

    clmap_t::iterator i = clmap.lower_bound(cpath);
    if (i != clmap.end() && i->first == cpath) {
        return i->second;
    }

    if (!parent_ns) {
        parent_ns = pyns;
    }

    // get relative path to class and class name
    std::string rpath_str;
    const char* rpath;
    const char* cname;
    {
        const char* p = strrchr(type->tp_name, '.');
        if (p) {
            rpath_str = std::string(type->tp_name, p - type->tp_name);
            rpath = rpath_str.c_str();
            cname = p + 1;
        } else {
            rpath = nullptr;
            cname = type->tp_name;
        }
    }

    // create new QorePythonClass
    QoreNamespace* ns;
    if (!rpath) {
        ns = parent_ns;
    } else {
        QoreString rel_ns_path(rpath);
        rel_ns_path.replaceAll(".", "::");
        ns = parent_ns->findCreateNamespacePathAll(rel_ns_path.c_str());
    }

    // create new class
    std::unique_ptr<QorePythonClass> cls(new QorePythonClass(cname));

    // insert into map
    clmap.insert(i, clmap_t::value_type(cpath, cls.get()));

    //printd(5, "QorePythonProgram::getCreateQorePythonClassIntern() parent ns: '%s' ns: '%s' cls: '%s' (%s)\n", parent_ns->getName(), ns->getName(), cls->getName(), cpath.c_str());

    cls->addConstructor((void*)type, (q_external_constructor_t)execPythonConstructor, Public,
            QCF_USES_EXTRA_ARGS, QDOM_UNCONTROLLED_API);

    ns->addSystemClass(cls.get());

    // iterate base classes
    for (Py_ssize_t i = 0, end = PyTuple_GET_SIZE(type->tp_bases); i < end; ++i) {
        PyTypeObject* b = reinterpret_cast<PyTypeObject*>(PyTuple_GET_ITEM(type->tp_bases, i));
        assert(PyType_Check(b));

        QorePythonClass* bclass = getCreateQorePythonClassIntern(xsink, b);
        if (!bclass) {
            assert(*xsink);
            return nullptr;
        }

        printd(5, "QorePythonProgram::getCreateQorePythonClassIntern() %s parent: %s (bclass: %p)\n",
            cpath.c_str(), b->tp_name, bclass);
        cls->addBuiltinVirtualBaseClass(bclass);
    }

    printd(5, "QorePythonProgram::getCreateQorePythonClassIntern() %s methods: %p\n", cpath.c_str(),
        type->tp_methods);

    // process dict
    if (type->tp_dict) {
        PyObject* key, * value;
        Py_ssize_t pos = 0;

        while (PyDict_Next(type->tp_dict, &pos, &key, &value)) {
            assert(Py_TYPE(key) == &PyUnicode_Type);
            const char* keystr = PyUnicode_AsUTF8(key);

            PyTypeObject* var_type = Py_TYPE(value);
            // check for static method
            if (var_type == &PyStaticMethod_Type) {
                // get callable from static method
                PyObject* py_method = PyStaticMethod_Type.tp_descr_get(value, nullptr, nullptr);
                assert(py_method);
                cls->addObj(py_method);
                cls->addStaticMethod((void*)py_method, keystr,
                    (q_external_static_method_t)QorePythonProgram::execPythonStaticMethod,
                    Public, QCF_USES_EXTRA_ARGS, QDOM_UNCONTROLLED_API, autoTypeInfo);
                printd(5, "QorePythonProgram::getCreateQorePythonClassIntern() added static method " \
                    "%s.%s() (%s)\n", cpath.c_str(), keystr, Py_TYPE(value)->tp_name);
                continue;
            }
            // check for wrapper descriptors -> normal method
            if (var_type == &PyWrapperDescr_Type) {
                // do not need to save reference here
                cls->addMethod((void*)value, keystr,
                    (q_external_method_t)QorePythonProgram::execPythonNormalWrapperDescriptorMethod,
                    Public, QCF_USES_EXTRA_ARGS, QDOM_UNCONTROLLED_API, autoTypeInfo);
                printd(5, "QorePythonProgram::getCreateQorePythonClassIntern() added normal wrapper " \
                    "descriptor method %s.%s() (%s) %p: %d\n", cpath.c_str(), keystr, Py_TYPE(value)->tp_name, value,
                    value->ob_refcnt);
                continue;
            }
            // check for method descriptors -> normal method
            if (var_type == &PyMethodDescr_Type) {
                // do not need to save reference here
                cls->addMethod((void*)value, keystr,
                    (q_external_method_t)QorePythonProgram::execPythonNormalMethodDescriptorMethod,
                    Public, QCF_USES_EXTRA_ARGS, QDOM_UNCONTROLLED_API, autoTypeInfo);
                printd(5, "QorePythonProgram::getCreateQorePythonClassIntern() added normal method " \
                    "descriptor method %s.%s() (%s) %p: %d\n", cpath.c_str(), keystr, Py_TYPE(value)->tp_name, value,
                    value->ob_refcnt);
                continue;
            }
            // check for classmethod descriptors -> normal method
            if (var_type == &PyClassMethodDescr_Type) {
                // do not need to save reference here
                cls->addMethod((void*)value, keystr,
                    (q_external_method_t)QorePythonProgram::execPythonNormalClassMethodDescriptorMethod,
                    Public, QCF_USES_EXTRA_ARGS, QDOM_UNCONTROLLED_API, autoTypeInfo);
                printd(5, "QorePythonProgram::getCreateQorePythonClassIntern() added normal "
                    "classmethod descriptor method %s.%s() (%s)\n", cpath.c_str(), keystr, Py_TYPE(value)->tp_name);
                continue;
            }
            // check for normal user methods
            if (PyFunction_Check(value)) {
                Py_INCREF(value);
                cls->addObj(value);
                cls->addMethod((void*)value, keystr,
                    (q_external_method_t)QorePythonProgram::execPythonNormalMethod, Public,
                    QCF_USES_EXTRA_ARGS, QDOM_UNCONTROLLED_API, autoTypeInfo);
                printd(5, "QorePythonProgram::getCreateQorePythonClassIntern() added normal method " \
                    "%s.%s() (%s)\n", cpath.c_str(), keystr, Py_TYPE(value)->tp_name);
                continue;
            }
            // check for builtin functions -> static method
            if (PyCFunction_Check(value)) {
                // do not need to save reference here
                cls->addStaticMethod((void*)value, keystr,
                    (q_external_static_method_t)QorePythonProgram::execPythonStaticCFunctionMethod,
                    Public, QCF_USES_EXTRA_ARGS, QDOM_UNCONTROLLED_API, autoTypeInfo);
                printd(5, "QorePythonProgram::getCreateQorePythonClassIntern() added static C function method " \
                    "%s.%s() (%s)\n", cpath.c_str(), keystr, Py_TYPE(value)->tp_name);
                continue;
            }
            // check for member descriptors
            if (var_type == &PyMemberDescr_Type) {
                cls->addPythonMember(keystr, reinterpret_cast<PyMemberDescrObject*>(value)->d_member);
                continue;
            }

            printd(5, "QorePythonProgram::getCreateQorePythonClassIntern() %s: member '%s': %s\n", cpath.c_str(), keystr, Py_TYPE(value)->tp_name);

            // add member
            cls->addMember(keystr, Public, autoTypeInfo);
        }
    }

    return cls.release();
}

QoreValue QorePythonProgram::execPythonStaticCFunctionMethod(const QoreMethod& meth, PyObject* func,
    const QoreListNode* args, q_rt_flags_t rtflags, ExceptionSink* xsink) {
    QorePythonProgram* pypgm = QorePythonProgram::getPythonProgramFromMethod(meth, xsink);
    if (!pypgm) {
        assert(*xsink);
        return QoreValue();
    }
    return pypgm->callCFunctionMethod(xsink, func, args);
}

QoreValue QorePythonProgram::execPythonCFunction(PyObject* func, const QoreListNode* args, q_rt_flags_t rtflags, ExceptionSink* xsink) {
    QorePythonProgram* pypgm = QorePythonProgram::getContext();
    return pypgm->callCFunctionMethod(xsink, func, args);
}

QoreValue QorePythonProgram::callCFunctionMethod(ExceptionSink* xsink, PyObject* func, const QoreListNode* args, size_t arg_offset) {
    QorePythonReferenceHolder py_args;
    if (args && args->size() > arg_offset) {
        py_args = getPythonTupleValue(xsink, args, arg_offset);
        if (*xsink) {
            return QoreValue();
        }
    } else {
        py_args = PyTuple_New(0);
    }

    QorePythonReferenceHolder return_value;
    {
        //printd(5, "QorePythonProgram::callCMethod(): calling '%s' argcount: %d\n", fname->c_str(), (args && args->size() > arg_offset) ? args->size() - arg_offset : 0);
        QorePythonHelper qph(python);
        return_value = PyCFunction_Call(func, *py_args, nullptr);

        // check for Python exceptions
        if (!return_value && checkPythonException(xsink)) {
            return QoreValue();
        }
    }

    return getQoreValue(*return_value, xsink);
}

void QorePythonProgram::execPythonConstructor(const QoreMethod& meth, PyObject* pycls, QoreObject* self,
    const QoreListNode* args, q_rt_flags_t rtflags, ExceptionSink* xsink) {
    QorePythonProgram* pypgm = QorePythonProgram::getPythonProgramFromMethod(meth, xsink);
    if (!pypgm) {
        assert(*xsink);
        return;
    }

    QorePythonReferenceHolder pyobj(pypgm->callPythonInternal(xsink, pycls, args));
    if (*xsink) {
        return;
    }

    self->setPrivate(meth.getClass()->getID(), new QorePythonPrivateData(pyobj.release()));
}

QoreValue QorePythonProgram::execPythonStaticMethod(const QoreMethod& meth, PyObject* m,
    const QoreListNode* args, q_rt_flags_t rtflags, ExceptionSink* xsink) {
    QorePythonProgram* pypgm = QorePythonProgram::getPythonProgramFromMethod(meth, xsink);
    if (!pypgm) {
        assert(*xsink);
        return QoreValue();
    }
    return pypgm->callInternal(xsink, m, args);
}

QoreValue QorePythonProgram::execPythonNormalMethod(const QoreMethod& meth, PyObject* m, QoreObject* self,
    QorePythonPrivateData* pd, const QoreListNode* args, q_rt_flags_t rtflags, ExceptionSink* xsink) {
    QorePythonProgram* pypgm = QorePythonProgram::getPythonProgramFromMethod(meth, xsink);
    if (!pypgm) {
        assert(*xsink);
        return QoreValue();
    }
    return pypgm->callInternal(xsink, m, args, 0, pd->get());
}

QoreValue QorePythonProgram::execPythonNormalWrapperDescriptorMethod(const QoreMethod& meth, PyObject* m,
    QoreObject* self, QorePythonPrivateData* pd, const QoreListNode* args, q_rt_flags_t rtflags,
    ExceptionSink* xsink) {
    //printd(5, "QorePythonProgram::execPythonNormalWrapperDescriptorMethod() %s::%s() pyobj: %p: %d\n", meth.getClassName(), meth.getName(), m, m->ob_refcnt);
    assert(m->ob_refcnt > 0);
    QorePythonProgram* pypgm = QorePythonProgram::getPythonProgramFromMethod(meth, xsink);
    if (!pypgm) {
        assert(*xsink);
        return QoreValue();
    }
    return pypgm->callWrapperDescriptorMethod(xsink, pd->get(), m, args);
}

QoreValue QorePythonProgram::execPythonNormalMethodDescriptorMethod(const QoreMethod& meth, PyObject* m,
    QoreObject* self, QorePythonPrivateData* pd, const QoreListNode* args, q_rt_flags_t rtflags,
    ExceptionSink* xsink) {
    //printd(5, "QorePythonProgram::execPythonNormalMethodDescriptorMethod() %s::%s() pyobj: %p: %d\n", meth.getClassName(), meth.getName(), m, m->ob_refcnt);
    QorePythonProgram* pypgm = QorePythonProgram::getPythonProgramFromMethod(meth, xsink);
    if (!pypgm) {
        assert(*xsink);
        return QoreValue();
    }
    return pypgm->callMethodDescriptorMethod(xsink, pd->get(), m, args);
}

QoreValue QorePythonProgram::execPythonNormalClassMethodDescriptorMethod(const QoreMethod& meth, PyObject* m,
    QoreObject* self, QorePythonPrivateData* pd, const QoreListNode* args, q_rt_flags_t rtflags,
    ExceptionSink* xsink) {
    QorePythonProgram* pypgm = QorePythonProgram::getPythonProgramFromMethod(meth, xsink);
    if (!pypgm) {
        assert(*xsink);
        return QoreValue();
    }
    return pypgm->callClassMethodDescriptorMethod(xsink, pd->get(), m, args);
}

QoreValue QorePythonProgram::callWrapperDescriptorMethod(ExceptionSink* xsink, PyObject* self, PyObject* obj,
    const QoreListNode* args, size_t arg_offset) {
    QorePythonReferenceHolder py_args;
    if (self || (args && args->size() > arg_offset)) {
        py_args = getPythonTupleValue(xsink, args, arg_offset, self);
        if (*xsink) {
            return QoreValue();
        }
    }

    QorePythonReferenceHolder return_value;
    {
        //printd(5, "QorePythonProgram::callWrapperDescriptorMethod(): calling '%s' argcount: %d\n", fname->c_str(), (args && args->size() > arg_offset) ? args->size() - arg_offset : 0);
        QorePythonHelper qph(python);
        return_value = PyWrapperDescr_Type.tp_call(obj, *py_args, nullptr);

        // check for Python exceptions
        if (!return_value && checkPythonException(xsink)) {
            return QoreValue();
        }
    }

    return getQoreValue(*return_value, xsink);
}

QoreValue QorePythonProgram::callMethodDescriptorMethod(ExceptionSink* xsink, PyObject* self, PyObject* obj,
    const QoreListNode* args, size_t arg_offset) {
    QorePythonReferenceHolder py_args;
    if (self || (args && args->size() > arg_offset)) {
        py_args = getPythonTupleValue(xsink, args, arg_offset, self);
        if (*xsink) {
            return QoreValue();
        }
    }

    QorePythonReferenceHolder return_value;
    {
        //printd(5, "QorePythonProgram::callMethodDescriptorMethod(): calling '%s' argcount: %d\n", fname->c_str(), (args && args->size() > arg_offset) ? args->size() - arg_offset : 0);
        QorePythonHelper qph(python);
        return_value = PyMethodDescr_Type.tp_call(obj, *py_args, nullptr);

        // check for Python exceptions
        if (!return_value && checkPythonException(xsink)) {
            return QoreValue();
        }
    }

    return getQoreValue(*return_value, xsink);
}

QoreValue QorePythonProgram::callClassMethodDescriptorMethod(ExceptionSink* xsink, PyObject* self, PyObject* obj,
    const QoreListNode* args, size_t arg_offset) {
    // get class from self
    QorePythonReferenceHolder cls(PyObject_GetAttrString(self, "__class__"));

    QorePythonReferenceHolder py_args;
    if (self || (args && args->size() > arg_offset)) {
        py_args = getPythonTupleValue(xsink, args, arg_offset, *cls);
        if (*xsink) {
            return QoreValue();
        }
    }

    QorePythonReferenceHolder return_value;
    {
        //printd(5, "QorePythonProgram::callClassMethodDescriptorMethod(): calling '%s' argcount: %d\n", fname->c_str(), (args && args->size() > arg_offset) ? args->size() - arg_offset : 0);
        QorePythonHelper qph(python);
        return_value = PyClassMethodDescr_Type.tp_call(obj, *py_args, nullptr);

        // check for Python exceptions
        if (!return_value && checkPythonException(xsink)) {
            return QoreValue();
        }
    }

    return getQoreValue(*return_value, xsink);
}

int QorePythonProgram::import(ExceptionSink* xsink, const char* module, const char* symbol) {
    QorePythonHelper qph(python);
    QorePythonReferenceHolder mod(PyImport_ImportModule(module));
    if (!mod) {
        throw QoreStandardException("PYTHON-IMPORT-ERROR", "Python could not load module '%s'", module);
    }

    PythonModuleContextHelper mch(this, module);

    // returns a borrowed reference
    PyObject* mod_dict = PyModule_GetDict(*mod);

    QoreNamespace* ns = pyns->findCreateNamespacePathAll(module);

    if (symbol) {
        // returns a borrowed reference
        PyObject* value = PyDict_GetItemString(mod_dict, symbol);
        if (!value) {
            throw QoreStandardException("PYTHON-IMPORT-ERROR", "symbol '%s' was not found in module \"%s\"'s dictionary", symbol, module);
        }
        if (importSymbol(xsink, value, ns, module, symbol)) {
            return -1;
        }
    } else {
        PyObject* key, * value;
        Py_ssize_t pos = 0;
        while (PyDict_Next(mod_dict, &pos, &key, &value)) {
            assert(Py_TYPE(key) == &PyUnicode_Type);
            const char* keystr = PyUnicode_AsUTF8(key);
            if (importSymbol(xsink, value, ns, module, keystr)) {
                return -1;
            }
        }
    }

    return 0;
}

int QorePythonProgram::importSymbol(ExceptionSink* xsink, PyObject* value, QoreNamespace* ns, const char* module,
    const char* symbol) {

    // check for builtin functions -> static method
    if (PyCFunction_Check(value)) {
        // do not need to save reference here
        ns->addBuiltinVariant((void*)value, symbol,
            (q_external_func_t)QorePythonProgram::execPythonCFunction, QCF_USES_EXTRA_ARGS, QDOM_UNCONTROLLED_API,
            autoTypeInfo);
        printd(5, "QorePythonProgram::importSymbol() added C function method %s::%s() (%s) (x: %d %s)\n", module, symbol,
            Py_TYPE(value)->tp_name, owns_qore_program_ref, ns->getName());
        return 0;
    }

    if (PyType_Check(value)) {
        QorePythonClass* cls = getCreateQorePythonClassIntern(xsink, reinterpret_cast<PyTypeObject*>(value), ns);
        if (*xsink) {
            assert(!cls);
            return -1;
        }

        //printd(5, "QorePythonProgram::importSymbol() added class %s.%s (%s)\n", module, symbol, cls->getName());
        return 0;
    }

    ValueHolder v(getQoreValue(value, xsink), xsink);
    if (*xsink) {
        return -1;
    }
    const QoreTypeInfo* typeInfo = v->getFullTypeInfo();
    //printd(5, "QorePythonProgram::importSymbol() adding const %s.%s = '%s'\n", module, symbol, qore_type_get_name(typeInfo));
    ns->addConstant(symbol, v.release(), typeInfo);
    return 0;
}
