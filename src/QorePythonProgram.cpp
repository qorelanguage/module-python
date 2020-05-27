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

// from Python internal code
PyThreadState* _qore_PyRuntimeGILState_GetThreadState(_gilstate_runtime_state& gilstate) {
    return reinterpret_cast<PyThreadState*>(_Py_atomic_load_relaxed(&gilstate.tstate_current));
}

PyThreadState* _qore_PyGILState_GetThisThreadState(_gilstate_runtime_state& gilstate) {
    assert(gilstate.autoInterpreterState);
    return reinterpret_cast<PyThreadState*>(PyThread_tss_get(&gilstate.autoTSSkey));
}

void _qore_PyGILState_SetThisThreadState(PyThreadState* state, _gilstate_runtime_state& gilstate) {
    PyThread_tss_set(&_PyRuntime.gilstate.autoTSSkey, (void*)state);
}

// from Python internal code
bool _qore_PyGILState_Check() {
    _gilstate_runtime_state& gilstate = _PyRuntime.gilstate;
    if (!PyThread_tss_is_created(&gilstate.autoTSSkey)) {
        return true;
    }

    PyThreadState* tstate = _qore_PyRuntimeGILState_GetThreadState(gilstate);
    if (tstate == NULL) {
        return false;
    }

    return (tstate == _qore_PyGILState_GetThisThreadState(gilstate));
}

#ifdef DEBUG
// from Python internal code
static bool _qore_PyThreadState_IsCurrent(PyThreadState* tstate) {
    // Must be the tstate for this thread
    return tstate == _qore_PyRuntimeGILState_GetThreadState();
}
#endif

// static member declarations
QorePythonProgram::py_thr_map_t QorePythonProgram::py_thr_map;
QoreThreadLock QorePythonProgram::py_thr_lck;

void QorePythonProgram::staticInit() {
    PyDateTime_IMPORT;
}

void QorePythonProgram::pythonThreadCleanup(void*) {
    int tid = gettid();
    AutoLocker al(py_thr_lck);

    // delete all thread states for the tid
    for (auto& i : py_thr_map) {
        py_tid_map_t::iterator ti = i.second.find(tid);
        if (ti != i.second.end()) {
            printd(5, "QorePythonProgram::pythonThreadCleanup() deleting thread state %p for TID %d", ti->second, tid);
            // delete thread state
            QorePythonGilHelper pgh;
            PyThreadState_Clear(ti->second);
            PyThreadState_Delete(ti->second);
            i.second.erase(ti);
        }
    }
}

int QorePythonProgram::createInterpreter(ExceptionSink* xsink) {
    assert(PyGILState_Check());
    PyThreadState* python = Py_NewInterpreter();
    if (!python) {
        if (xsink) {
            xsink->raiseException("PYTHON-COMPILE-ERROR", "error creating the Python subinterpreter");
        }
        return -1;
    }
    assert(python->gilstate_counter == 1);
    //printd(5, "QorePythonProgram::createInterpreter() created thead state: %p\n", python);

    // NOTE: we have to reenable PyFILState_Check() here
    assert(!_PyRuntime.gilstate.check_enabled);
    _PyRuntime.gilstate.check_enabled = 1;

    _qore_PyGILState_SetThisThreadState(python);

    interpreter = python->interp;
    // save thread state
    int tid = gettid();
    AutoLocker al(py_thr_lck);
    assert(py_thr_map.find(this) == py_thr_map.end());
    py_thr_map[this] = {{tid, python}};
    return 0;
}

/*
int QorePythonProgram::createInterpreter(ExceptionSink* xsink) {
    assert(PyGILState_Check());
    PyThreadState* tcur = _qore_PyRuntimeGILState_GetThreadState();
    ON_BLOCK_EXIT(PyThreadState_Swap, tcur);

    return createInterpreterIntern(xsink);
}
*/

QorePythonThreadInfo QorePythonProgram::setContext() const {
    if (!valid) {
        return {nullptr, PyGILState_UNLOCKED, false};
    }
    assert(interpreter);
    PyThreadState* python = getThreadState();
    // create new thread state if necessary
    if (!python) {
        python = PyThreadState_New(interpreter);
        //printd(5, "QorePythonProgram::setContext() created new thread context: %p\n", python);
        assert(python);
        assert(!python->gilstate_counter);
        // the thread state will be deleted when the thread terminates or the interpreter is deleted
        AutoLocker al(py_thr_lck);
        py_thr_map_t::iterator i = py_thr_map.find(this);
        if (i == py_thr_map.end()) {
            py_thr_map[this] = {{gettid(), python}};
        } else {
            assert(i->second.find(gettid()) == i->second.end());
            i->second[gettid()] = python;
        }
    }

    //printd(5, "QorePythonProgram::setContext() got thread context: %p (GIL: %d) refs: %d\n", python, PyGILState_Check(), python->gilstate_counter);

    PyGILState_STATE g_state;
    PyThreadState* t_state;
    if (PyGILState_Check()) {
        t_state = _qore_PyRuntimeGILState_GetThreadState();
        if (t_state != python) {
            PyThreadState_Swap(python);
        }
        g_state = PyGILState_LOCKED;
    } else {
        assert(!_qore_PyRuntimeGILState_GetThreadState());
        assert(!_qore_PyGILState_GetThisThreadState());

        t_state = nullptr;
        PyEval_RestoreThread(python);
        g_state = PyGILState_UNLOCKED;
    }

    // set new thread state
    _qore_PyGILState_SetThisThreadState(python);

    assert(PyGILState_Check());

    //printd(5, "QorePythonProgram::setContext() old thread context: %p\n", t_state);

    /*
    if (!oldstate) {
        assert(!PyGILState_Check());
        assert(!python->gilstate_counter);
        // acquire GIL and set this thread state as current
        PyEval_RestoreThread(python);
    } else if (oldstate != python) {
        assert(PyGILState_Check());
        PyThreadState_Swap(python);
    } else {
        assert(PyGILState_Check());
    }
    */

    ++python->gilstate_counter;

    return {t_state, g_state, true};
}

void QorePythonProgram::releaseContext(const QorePythonThreadInfo& oldstate) const {
    if (!oldstate.valid) {
        return;
    }
    struct _gilstate_runtime_state* gilstate = &_PyRuntime.gilstate;
    PyThreadState* python = getThreadState();
    assert(python);
    assert(_qore_PyThreadState_IsCurrent(python));
    assert(python->gilstate_counter > 0);
    assert(PyGILState_Check());

    --python->gilstate_counter;

    //printd(5, "QorePythonProgram::releaseContext() t_state: %p g_state: %d\n", oldstate.t_state, oldstate.g_state);

    if (oldstate.g_state == PyGILState_UNLOCKED) {
        if (!oldstate.t_state) {
            PyEval_ReleaseThread(python);
        } else {
            assert(false);
        }
        assert(!PyGILState_Check());
        PyThreadState* state = _qore_PyGILState_GetThisThreadState();
        if (state) {
            _qore_PyGILState_SetThisThreadState(nullptr);
        }
    } else {
        // restore old thread context; GIL still held
        if (python != oldstate.t_state) {
            PyThreadState_Swap(oldstate.t_state);
            _qore_PyGILState_SetThisThreadState(oldstate.t_state);
        }
    }
}

QoreListNode* QorePythonProgram::getQoreListFromList(ExceptionSink* xsink, PyObject* val) {
    pyobj_set_t rset;
    return getQoreListFromList(xsink, val, rset);
}

QoreListNode* QorePythonProgram::getQoreListFromList(ExceptionSink* xsink, PyObject* val, pyobj_set_t& rset) {
    assert(PyList_Check(val));
    ReferenceHolder<QoreListNode> rv(new QoreListNode(autoTypeInfo), xsink);
    Py_ssize_t len = PyList_Size(val);
    for (Py_ssize_t i = 0; i < len; ++i) {
        ValueHolder qval(getQoreValue(xsink, PyList_GetItem(val, i), rset), xsink);
        if (*xsink) {
            return nullptr;
        }
        rv->push(qval.release(), xsink);
        assert(!*xsink);
    }
    return rv.release();
}

QoreListNode* QorePythonProgram::getQoreListFromTuple(ExceptionSink* xsink, PyObject* val) {
    pyobj_set_t rset;
    return getQoreListFromTuple(xsink, val, rset);
}

QoreListNode* QorePythonProgram::getQoreListFromTuple(ExceptionSink* xsink, PyObject* val, pyobj_set_t& rset) {
    assert(PyTuple_Check(val));
    ReferenceHolder<QoreListNode> rv(new QoreListNode(autoTypeInfo), xsink);
    Py_ssize_t len = PyTuple_Size(val);
    for (Py_ssize_t i = 0; i < len; ++i) {
        ValueHolder qval(getQoreValue(xsink, PyTuple_GetItem(val, i), rset), xsink);
        if (*xsink || checkPythonException(xsink)) {
            return nullptr;
        }
        rv->push(qval.release(), xsink);
        assert(!*xsink);
    }
    return rv.release();
}

QoreHashNode* QorePythonProgram::getQoreHashFromDict(ExceptionSink* xsink, PyObject* val, pyobj_set_t& rset) {
    assert(PyDict_Check(val));
    assert(rset.find(val) == rset.end());
    rset.insert(val);

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

        // skip recursive refs
        if (PyDict_Check(value) && (rset.find(value) != rset.end())) {
            continue;
        }

        ValueHolder qval(getQoreValue(xsink, value, rset), xsink);
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

QoreValue QorePythonProgram::getQoreValue(ExceptionSink* xsink, QorePythonReferenceHolder& val) {
    return getQoreValue(xsink, *val);
}

QoreValue QorePythonProgram::getQoreValue(ExceptionSink* xsink, QorePythonReferenceHolder& val, pyobj_set_t& rset) {
    return getQoreValue(xsink, *val, rset);
}

QoreValue QorePythonProgram::getQoreValue(ExceptionSink* xsink, PyObject* val) {
    pyobj_set_t rset;
    return getQoreValue(xsink, val, rset);
}

QoreValue QorePythonProgram::getQoreValue(ExceptionSink* xsink, PyObject* val, pyobj_set_t& rset) {
    //printd(5, "QorePythonBase::getQoreValue() val: %p\n", val);
    if (!val || val == Py_None) {
        return QoreValue();
    }

    PyTypeObject* type = Py_TYPE(val);
    if (type == &PyBool_Type) {
        return QoreValue(val == Py_True);
    }

    if (type == &PyLong_Type) {
        // Python 3+ implements "long" as an arbitrary-precision number (= Qore "number")
        QorePythonReferenceHolder longval(PyObject_Repr(val));
        assert(Py_TYPE(*longval) == &PyUnicode_Type);
        const char* longstr = PyUnicode_AsUTF8(*longval);

        // see if we can convert to a Qore integer
        bool sign = longstr[0] == '-';
        if ((!sign && strcmp(longstr, "9223372036854775807") <= 0)
            || (sign && strcmp(longstr, "-9223372036854775808") <= 0)) {
            return strtoll(longstr, 0, 10);
        }
        return new QoreNumberNode(longstr);
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
        return getQoreListFromList(xsink, val, rset);
    }

    if (type == &PyTuple_Type) {
        return getQoreListFromTuple(xsink, val, rset);
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
        return getQoreHashFromDict(xsink, val, rset);
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
    QorePythonReferenceHolder return_value(PyObject_GetAttrString(obj, attr));
    // check for Python exceptions
    if (!return_value && checkPythonException(xsink)) {
        return QoreValue();
    }

    return getQoreValue(xsink, *return_value);
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
        return PyTuple_New(0);
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

        case NT_OBJECT: {
            QoreObject* o = const_cast<QoreObject*>(val.get<const QoreObject>());
            if (!o->isValid()) {
                Py_INCREF(Py_None);
                return Py_None;
            }

            TryPrivateDataRefHolder<QorePythonPrivateData> pypd(o, CID_PYTHONBASEOBJECT, xsink);
            if (pypd) {
                PyObject* rv = pypd->get();
                Py_XINCREF(rv);
                return rv;
            }
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

    QorePythonHelper qph(this);
    if (checkValid(xsink)) {
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
    QorePythonHelper qph(this);
    if (checkValid(xsink)) {
        return QoreValue();
    }

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
    QorePythonHelper qph(this);
    if (checkValid(xsink)) {
        return QoreValue();
    }
    QorePythonReferenceHolder rv(callPythonInternal(xsink, callable, args, arg_offset, first));
    return *xsink ? QoreValue() : getQoreValue(xsink, rv.release());
}

PyObject* QorePythonProgram::callPythonInternal(ExceptionSink* xsink, PyObject* callable, const QoreListNode* args,
    size_t arg_offset, PyObject* first) {
    QorePythonReferenceHolder py_args;

    py_args = getPythonTupleValue(xsink, args, arg_offset, first);
    if (*xsink) {
        return nullptr;
    }

    //printd(5, "QorePythonProgram::callPythonInternal(): this: %p valid: %d argcount: %d\n", this, valid, (args && args->size() > arg_offset) ? args->size() - arg_offset : 0);
    QorePythonReferenceHolder return_value(PyEval_CallObject(callable, *py_args));
    // check for Python exceptions
    if (!return_value && checkPythonException(xsink)) {
        return nullptr;
    }

    return return_value.release();
}

QoreValue QorePythonProgram::callFunctionObject(ExceptionSink* xsink, PyObject* func, const QoreListNode* args,
    size_t arg_offset, PyObject* first) {
    QorePythonHelper qph(this);
    if (checkValid(xsink)) {
        return QoreValue();
    }
    QorePythonReferenceHolder py_args(getPythonTupleValue(xsink, args, arg_offset, first));
    if (*xsink) {
        return QoreValue();
    }

    //printd(5, "QorePythonProgram::callFunctionObject(): this: %p valid: %d argcount: %d\n", this, valid, (args && args->size() > arg_offset) ? args->size() - arg_offset : 0);
    QorePythonReferenceHolder return_value(PyFunction_Type.tp_call(func, *py_args, nullptr));
    // check for Python exceptions
    if (!return_value && checkPythonException(xsink)) {
        return QoreValue();
    }
    return getQoreValue(xsink, return_value.release());
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
    ValueHolder qore_desc(getQoreValue(xsink, *desc), xsink);
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
        return callFunctionObject(xsink, attr, args, arg_offset, obj);
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
    QoreNamespace* parent_ns, const char* cname) {
    //printd(5, "QorePythonProgram::getCreateQorePythonClassIntern() class: '%s'\n", type->tp_name);

    clmap_t::iterator i = clmap.lower_bound(type);
    if (i != clmap.end() && i->first == type) {
        return i->second;
    }

    if (!parent_ns) {
        parent_ns = pyns;
    }

    // get relative path to class and class name
    std::string rpath_str;
    const char* rpath;
    {
        const char* p = strrchr(type->tp_name, '.');
        if (p) {
            rpath_str = std::string(type->tp_name, p - type->tp_name);
            rpath = rpath_str.c_str();
            if (!cname) {
                cname = p + 1;
            }
        } else {
            rpath = nullptr;
            if (!cname) {
                cname = type->tp_name;
            }
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
    std::unique_ptr<QorePythonClass> cls(new QorePythonClass(this, cname));

    // insert into map
    clmap.insert(i, clmap_t::value_type(type, cls.get()));

    //printd(5, "QorePythonProgram::getCreateQorePythonClassIntern() parent ns: '%s' ns: '%s' cls: '%s' (%s)\n", parent_ns->getName(), ns->getName(), cls->getName(), type->tp_name);

    cls->addConstructor((void*)type, (q_external_constructor_t)execPythonConstructor, Public,
            QCF_USES_EXTRA_ARGS, QDOM_UNCONTROLLED_API);
    cls->setDestructor((void*)type, (q_external_destructor_t)execPythonDestructor);

    ns->addSystemClass(cls.get());

    // iterate base classes
    if (type->tp_bases) {
        for (Py_ssize_t i = 0, end = PyTuple_GET_SIZE(type->tp_bases); i < end; ++i) {
            PyTypeObject* b = reinterpret_cast<PyTypeObject*>(PyTuple_GET_ITEM(type->tp_bases, i));
            assert(PyType_Check(b));

            QorePythonClass* bclass = getCreateQorePythonClassIntern(xsink, b);
            if (!bclass) {
                assert(*xsink);
                return nullptr;
            }

            printd(5, "QorePythonProgram::getCreateQorePythonClassIntern() %s parent: %s (bclass: %p)\n",
                type->tp_name, b->tp_name, bclass);
            cls->addBuiltinVirtualBaseClass(bclass);
        }
    }

    cls->addBuiltinVirtualBaseClass(QC_PYTHONBASEOBJECT);

    printd(5, "QorePythonProgram::getCreateQorePythonClassIntern() %s methods: %p\n", type->tp_name,
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
                    "%s.%s() (%s)\n", type->tp_name, keystr, Py_TYPE(value)->tp_name);
                continue;
            }
            // check for wrapper descriptors -> normal method
            if (var_type == &PyWrapperDescr_Type) {
                // do not need to save reference here
                if (!strcmp(keystr, "copy")) {
                    keystr = "_copy";
                }
                cls->addMethod((void*)value, keystr,
                    (q_external_method_t)QorePythonProgram::execPythonNormalWrapperDescriptorMethod,
                    Public, QCF_USES_EXTRA_ARGS, QDOM_UNCONTROLLED_API, autoTypeInfo);
                printd(5, "QorePythonProgram::getCreateQorePythonClassIntern() added normal wrapper " \
                    "descriptor method %s.%s() (%s) %p: %d\n", type->tp_name, keystr, Py_TYPE(value)->tp_name, value,
                    value->ob_refcnt);
                continue;
            }
            // check for method descriptors -> normal method
            if (var_type == &PyMethodDescr_Type) {
                // do not need to save reference here
                if (!strcmp(keystr, "copy")) {
                    keystr = "_copy";
                }
                cls->addMethod((void*)value, keystr,
                    (q_external_method_t)QorePythonProgram::execPythonNormalMethodDescriptorMethod,
                    Public, QCF_USES_EXTRA_ARGS, QDOM_UNCONTROLLED_API, autoTypeInfo);
                printd(5, "QorePythonProgram::getCreateQorePythonClassIntern() added normal method " \
                    "descriptor method %s.%s() (%s) %p: %d\n", type->tp_name, keystr, Py_TYPE(value)->tp_name, value,
                    value->ob_refcnt);
                continue;
            }
            // check for classmethod descriptors -> normal method
            if (var_type == &PyClassMethodDescr_Type) {
                // do not need to save reference here
                if (!strcmp(keystr, "copy")) {
                    keystr = "_copy";
                }
                cls->addMethod((void*)value, keystr,
                    (q_external_method_t)QorePythonProgram::execPythonNormalClassMethodDescriptorMethod,
                    Public, QCF_USES_EXTRA_ARGS, QDOM_UNCONTROLLED_API, autoTypeInfo);
                printd(5, "QorePythonProgram::getCreateQorePythonClassIntern() added normal "
                    "classmethod descriptor method %s.%s() (%s)\n", type->tp_name, keystr, Py_TYPE(value)->tp_name);
                continue;
            }
            // check for normal user methods
            if (PyFunction_Check(value)) {
                Py_INCREF(value);
                cls->addObj(value);
                if (!strcmp(keystr, "copy")) {
                    keystr = "_copy";
                }
                cls->addMethod((void*)value, keystr,
                    (q_external_method_t)QorePythonProgram::execPythonNormalMethod, Public,
                    QCF_USES_EXTRA_ARGS, QDOM_UNCONTROLLED_API, autoTypeInfo);
                printd(5, "QorePythonProgram::getCreateQorePythonClassIntern() added normal method " \
                    "%s.%s() (%s)\n", type->tp_name, keystr, Py_TYPE(value)->tp_name);
                continue;
            }
            // check for builtin functions -> static method
            if (PyCFunction_Check(value)) {
                // do not need to save reference here
                cls->addStaticMethod((void*)value, keystr,
                    (q_external_static_method_t)QorePythonProgram::execPythonStaticCFunctionMethod,
                    Public, QCF_USES_EXTRA_ARGS, QDOM_UNCONTROLLED_API, autoTypeInfo);
                printd(5, "QorePythonProgram::getCreateQorePythonClassIntern() added static C function method " \
                    "%s.%s() (%s)\n", type->tp_name, keystr, Py_TYPE(value)->tp_name);
                continue;
            }
            // check for member descriptors
            if (var_type == &PyMemberDescr_Type) {
                cls->addPythonMember(keystr, reinterpret_cast<PyMemberDescrObject*>(value)->d_member);
                continue;
            }

            printd(5, "QorePythonProgram::getCreateQorePythonClassIntern() %s: member '%s': %s\n", type->tp_name, keystr, Py_TYPE(value)->tp_name);

            // do not add members
        }
    }

    return cls.release();
}

QoreValue QorePythonProgram::execPythonStaticCFunctionMethod(const QoreMethod& meth, PyObject* func,
    const QoreListNode* args, q_rt_flags_t rtflags, ExceptionSink* xsink) {
    QorePythonProgram* pypgm = QorePythonProgram::getPythonProgramFromMethod(meth, xsink);
    return pypgm->callCFunctionMethod(xsink, func, args);
}

QoreValue QorePythonProgram::execPythonCFunction(PyObject* func, const QoreListNode* args, q_rt_flags_t rtflags, ExceptionSink* xsink) {
    QorePythonProgram* pypgm = QorePythonProgram::getContext();
    return pypgm->callCFunctionMethod(xsink, func, args);
}

QoreValue QorePythonProgram::execPythonFunction(PyObject* func, const QoreListNode* args, q_rt_flags_t rtflags, ExceptionSink* xsink) {
    QorePythonProgram* pypgm = QorePythonProgram::getContext();
    return pypgm->callFunctionObject(xsink, func, args);
}

QoreValue QorePythonProgram::callCFunctionMethod(ExceptionSink* xsink, PyObject* func, const QoreListNode* args, size_t arg_offset) {
    QorePythonHelper qph(this);
    QorePythonReferenceHolder py_args;
    if (checkValid(xsink)) {
        return QoreValue();
    }

    py_args = getPythonTupleValue(xsink, args, arg_offset);
    if (*xsink) {
        return QoreValue();
    }

    //printd(5, "QorePythonProgram::callCMethod(): calling '%s' argcount: %d\n", fname->c_str(), (args && args->size() > arg_offset) ? args->size() - arg_offset : 0);
    QorePythonReferenceHolder return_value(PyCFunction_Call(func, *py_args, nullptr));
    // check for Python exceptions
    if (!return_value && checkPythonException(xsink)) {
        return QoreValue();
    }

    return getQoreValue(xsink, *return_value);
}

void QorePythonProgram::execPythonConstructor(const QoreMethod& meth, PyObject* pycls, QoreObject* self,
    const QoreListNode* args, q_rt_flags_t rtflags, ExceptionSink* xsink) {
    QorePythonProgram* pypgm = QorePythonProgram::getPythonProgramFromMethod(meth, xsink);
    QorePythonHelper qph(pypgm);
    if (pypgm->checkValid(xsink)) {
        return;
    }
    QorePythonReferenceHolder pyobj(pypgm->callPythonInternal(xsink, pycls, args));
    if (*xsink) {
        return;
    }

    self->setPrivate(meth.getClass()->getID(), new QorePythonPrivateData(pyobj.release()));
}

void QorePythonProgram::execPythonDestructor(const QorePythonClass& thisclass, PyObject* pycls, QoreObject* self,
    QorePythonPrivateData* pd, ExceptionSink* xsink) {
    QorePythonProgram* pypgm = thisclass.getPythonProgram();

    QorePythonHelper qph(pypgm);
    //assert(!pypgm->checkValid(xsink));

    pd->deref(xsink);
}

QoreValue QorePythonProgram::execPythonStaticMethod(const QoreMethod& meth, PyObject* m,
    const QoreListNode* args, q_rt_flags_t rtflags, ExceptionSink* xsink) {
    QorePythonProgram* pypgm = QorePythonProgram::getPythonProgramFromMethod(meth, xsink);
    return pypgm->callInternal(xsink, m, args);
}

QoreValue QorePythonProgram::execPythonNormalMethod(const QoreMethod& meth, PyObject* m, QoreObject* self,
    QorePythonPrivateData* pd, const QoreListNode* args, q_rt_flags_t rtflags, ExceptionSink* xsink) {
    QorePythonProgram* pypgm = QorePythonProgram::getPythonProgramFromMethod(meth, xsink);
    return pypgm->callInternal(xsink, m, args, 0, pd->get());
}

QoreValue QorePythonProgram::execPythonNormalWrapperDescriptorMethod(const QoreMethod& meth, PyObject* m,
    QoreObject* self, QorePythonPrivateData* pd, const QoreListNode* args, q_rt_flags_t rtflags,
    ExceptionSink* xsink) {
    //printd(5, "QorePythonProgram::execPythonNormalWrapperDescriptorMethod() %s::%s() pyobj: %p: %d\n", meth.getClassName(), meth.getName(), m, m->ob_refcnt);
    assert(m->ob_refcnt > 0);
    QorePythonProgram* pypgm = QorePythonProgram::getPythonProgramFromMethod(meth, xsink);
    return pypgm->callWrapperDescriptorMethod(xsink, pd->get(), m, args);
}

QoreValue QorePythonProgram::execPythonNormalMethodDescriptorMethod(const QoreMethod& meth, PyObject* m,
    QoreObject* self, QorePythonPrivateData* pd, const QoreListNode* args, q_rt_flags_t rtflags,
    ExceptionSink* xsink) {
    //printd(5, "QorePythonProgram::execPythonNormalMethodDescriptorMethod() %s::%s() pyobj: %p: %d\n", meth.getClassName(), meth.getName(), m, m->ob_refcnt);
    QorePythonProgram* pypgm = QorePythonProgram::getPythonProgramFromMethod(meth, xsink);
    return pypgm->callMethodDescriptorMethod(xsink, pd->get(), m, args);
}

QoreValue QorePythonProgram::execPythonNormalClassMethodDescriptorMethod(const QoreMethod& meth, PyObject* m,
    QoreObject* self, QorePythonPrivateData* pd, const QoreListNode* args, q_rt_flags_t rtflags,
    ExceptionSink* xsink) {
    QorePythonProgram* pypgm = QorePythonProgram::getPythonProgramFromMethod(meth, xsink);
    return pypgm->callClassMethodDescriptorMethod(xsink, pd->get(), m, args);
}

QoreValue QorePythonProgram::callWrapperDescriptorMethod(ExceptionSink* xsink, PyObject* self, PyObject* obj,
    const QoreListNode* args, size_t arg_offset) {
    QorePythonHelper qph(this);
    if (checkValid(xsink)) {
        return QoreValue();
    }
    QorePythonReferenceHolder py_args;
    py_args = getPythonTupleValue(xsink, args, arg_offset, self);
    if (*xsink) {
        return QoreValue();
    }

    //printd(5, "QorePythonProgram::callWrapperDescriptorMethod(): calling '%s' argcount: %d\n", fname->c_str(), (args && args->size() > arg_offset) ? args->size() - arg_offset : 0);
    QorePythonReferenceHolder return_value(PyWrapperDescr_Type.tp_call(obj, *py_args, nullptr));
    // check for Python exceptions
    if (!return_value && checkPythonException(xsink)) {
        return QoreValue();
    }

    return getQoreValue(xsink, *return_value);
}

QoreValue QorePythonProgram::callMethodDescriptorMethod(ExceptionSink* xsink, PyObject* self, PyObject* obj,
    const QoreListNode* args, size_t arg_offset) {
    QorePythonHelper qph(this);
    if (checkValid(xsink)) {
        return QoreValue();
    }
    QorePythonReferenceHolder py_args;

    py_args = getPythonTupleValue(xsink, args, arg_offset, self);
    if (*xsink) {
        return QoreValue();
    }

    //printd(5, "QorePythonProgram::callMethodDescriptorMethod(): calling '%s' argcount: %d\n", fname->c_str(), (args && args->size() > arg_offset) ? args->size() - arg_offset : 0);
    QorePythonReferenceHolder return_value(PyMethodDescr_Type.tp_call(obj, *py_args, nullptr));
    // check for Python exceptions
    if (!return_value && checkPythonException(xsink)) {
        return QoreValue();
    }

    return getQoreValue(xsink, *return_value);
}

QoreValue QorePythonProgram::callClassMethodDescriptorMethod(ExceptionSink* xsink, PyObject* self, PyObject* obj,
    const QoreListNode* args, size_t arg_offset) {
    QorePythonHelper qph(this);
    if (checkValid(xsink)) {
        return QoreValue();
    }

    // get class from self
    QorePythonReferenceHolder cls(PyObject_GetAttrString(self, "__class__"));

    QorePythonReferenceHolder py_args;
    py_args = getPythonTupleValue(xsink, args, arg_offset, *cls);
    if (*xsink) {
        return QoreValue();
    }

    //printd(5, "QorePythonProgram::callClassMethodDescriptorMethod(): calling '%s' argcount: %d\n", fname->c_str(), (args && args->size() > arg_offset) ? args->size() - arg_offset : 0);
    QorePythonReferenceHolder return_value(PyClassMethodDescr_Type.tp_call(obj, *py_args, nullptr));
    // check for Python exceptions
    if (!return_value && checkPythonException(xsink)) {
        return QoreValue();
    }

    return getQoreValue(xsink, *return_value);
}

int QorePythonProgram::import(ExceptionSink* xsink, const char* module, const char* symbol) {
    QorePythonReferenceHolder mod(PyImport_ImportModule(module));
    if (!mod) {
        if (!checkPythonException(xsink)) {
            throw QoreStandardException("PYTHON-IMPORT-ERROR", "Python could not load module '%s'", module);
        }
        return -1;
    }

    QoreString ns_path(module);

    if (symbol) {
        // returns a borrowed reference
        PyObject* mod_dict = PyModule_GetDict(*mod);

        QoreString sym(symbol);
        // find intermediate modules
        while (true) {
            qore_offset_t i = sym.find('.');
            if (i <= 0 || i == (sym.size() - 1)) {
                break;
            }
            QoreString mod_name(&sym, i);

            // returns a borrowed reference
            PyObject* mod_val = PyDict_GetItemString(mod_dict, mod_name.c_str());
            if (!mod_val) {
                throw QoreStandardException("PYTHON-IMPORT-ERROR", "submodule '%s' was not found in module " \
                    "\"%s\"'s dictionary", mod_name.c_str(), module);
            }
            if (!PyModule_Check(mod_val)) {
                throw QoreStandardException("PYTHON-IMPORT-ERROR", "'%s' is not a submodule but rather has " \
                    "type '%s'", mod_name.c_str(), Py_TYPE(mod_val)->tp_name);
            }

            ns_path.sprintf("::%s", mod_name.c_str());

            // returns a borrowed reference
            mod_dict = PyModule_GetDict(mod_val);
            sym.splice(0, i + 1, xsink);
            if (*xsink) {
                return -1;
            }
            symbol = sym.c_str();
        }

        // returns a borrowed reference
        PyObject* value = PyDict_GetItemString(mod_dict, symbol);
        if (!value) {
            throw QoreStandardException("PYTHON-IMPORT-ERROR", "symbol '%s' was not found in module \"%s\"'s " \
                "dictionary", symbol, module);
        }

        QoreNamespace* ns = pyns->findCreateNamespacePathAll(ns_path.c_str());
        pyobj_set_t mod_set;
        return importSymbol(xsink, value, ns, module, symbol, IF_ALL, mod_set);
    }

    // first import all classes
    {
        pyobj_set_t mod_set;
        if (importModule(xsink, *mod, module, IF_CLASS, mod_set)) {
            assert(*xsink);
            return -1;
        }
    }

    pyobj_set_t mod_set;
    return importModule(xsink, *mod, module, IF_OTHER, mod_set);
}

int QorePythonProgram::importModule(ExceptionSink* xsink, PyObject* mod, const char* module, int filter,
    pyobj_set_t& mod_set) {
    PythonModuleContextHelper mch(this, module);

    if (mod_set.find(mod) != mod_set.end()) {
        return 0;
    }
    mod_set.insert(mod);

    //printd(5, "QorePythonProgram::importModule() '%s' mod: %p (%d)\n", module, mod, filter);

    // returns a borrowed reference
    PyObject* mod_dict = PyModule_GetDict(mod);

    QoreNamespace* ns = pyns->findCreateNamespacePathAll(module);

    PyObject* key, * value;
    Py_ssize_t pos = 0;
    // first import classes and modules
    while (PyDict_Next(mod_dict, &pos, &key, &value)) {
        bool is_class = PyType_Check(value);
        if (is_class) {
            if (!(filter & IF_CLASS)) {
                continue;
            }
        } else if (!PyModule_Check(value) && !(filter & IF_OTHER)) {
            continue;
        }

        assert(Py_TYPE(key) == &PyUnicode_Type);
        const char* keystr = PyUnicode_AsUTF8(key);
        if (importSymbol(xsink, value, ns, module, keystr, filter, mod_set)) {
            return -1;
        }
    }

    return 0;
}

int QorePythonProgram::importSymbol(ExceptionSink* xsink, PyObject* value, QoreNamespace* ns, const char* module,
    const char* symbol, int filter, pyobj_set_t& mod_set) {
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

    if (PyFunction_Check(value)) {
        ns->addBuiltinVariant((void*)value, symbol,
            (q_external_func_t)QorePythonProgram::execPythonFunction, QCF_USES_EXTRA_ARGS, QDOM_UNCONTROLLED_API,
            autoTypeInfo);

        //printd(5, "QorePythonProgram::importSymbol() added function %s::%s() (%s) (x: %d %s)\n", module, symbol, Py_TYPE(value)->tp_name, owns_qore_program_ref, ns->getName());
        return 0;
    }

    if (PyType_Check(value)) {
        PyTypeObject* type = reinterpret_cast<PyTypeObject*>(value);
        //printd(5, "QorePythonProgram::importSymbol() class sym: '%s' -> '%s' (%p)\n", symbol, type->tp_name, type);
        QorePythonClass* cls = getCreateQorePythonClassIntern(xsink, reinterpret_cast<PyTypeObject*>(value), ns);
        if (*xsink) {
            assert(!cls);
            return -1;
        }

        //printd(5, "QorePythonProgram::importSymbol() added class %s.%s (%s)\n", module, symbol, cls->getName());
        return 0;
    }

    if (PyModule_Check(value)) {
        QoreStringMaker sub_module("%s::%s", module, symbol);
        return importModule(xsink, value, sub_module.c_str(), filter, mod_set);
    }

    //printd(5, "QorePythonProgram::importSymbol() adding const %s.%s = '%s'\n", module, symbol, Py_TYPE(value)->tp_name);
    ValueHolder v(getQoreValue(xsink, value), xsink);
    if (*xsink) {
        return -1;
    }
    const QoreTypeInfo* typeInfo = v->getFullTypeInfo();

    //printd(5, "QorePythonProgram::importSymbol() adding const %s.%s = '%s'\n", module, symbol, qore_type_get_name(typeInfo));
    ns->addConstant(symbol, v.release(), typeInfo);
    return 0;
}
