/* -*- mode: c++; indent-tabs-mode: nil -*- */
/*
    QorePythonClass.h

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

#include "QorePythonClass.h"
#include "QorePythonProgram.h"

#include <structmember.h>

type_vec_t QorePythonClass::gateParamTypeInfo = { stringTypeInfo, boolOrNothingTypeInfo };

QorePythonClass::QorePythonClass(const char* name) : QoreBuiltinClass(name, QDOM_UNCONTROLLED_API), pypgm(nullptr) {
}

QorePythonClass::QorePythonClass(QorePythonProgram* pypgm, const char* name) : QoreBuiltinClass(name, QDOM_UNCONTROLLED_API), pypgm(pypgm) {
    addMethod(nullptr, "memberGate", (q_external_method_t)memberGate, Public, QCF_NO_FLAGS, QDOM_UNCONTROLLED_API,
        autoTypeInfo, gateParamTypeInfo);
    addMethod(nullptr, "methodGate", (q_external_method_t)methodGate, Public, QCF_USES_EXTRA_ARGS, QDOM_UNCONTROLLED_API,
        autoTypeInfo, gateParamTypeInfo);

    setPublicMemberFlag();
    setGateAccessFlag();
}

void QorePythonClass::addObj(PyObject* obj) {
    pypgm->addObj(obj);
}

// static method
QoreValue QorePythonClass::methodGate(const QoreMethod& meth, void* m, QoreObject* self, QorePythonPrivateData* pd,
    const QoreListNode* args, q_rt_flags_t rtflags, ExceptionSink* xsink) {
    assert(args && args->size() >= 2);
    assert(args->retrieveEntry(0).getType() == NT_STRING);
    assert(args->retrieveEntry(1).getType() == NT_BOOLEAN);

    const QoreStringNode* mname = args->retrieveEntry(0).get<QoreStringNode>();
    bool cls_access = args->retrieveEntry(1).getAsBool();

    QorePythonProgram* pypgm = QorePythonProgram::getPythonProgramFromMethod(meth, xsink);
    if (!pypgm) {
        assert(*xsink);
        return QoreValue();
    }

    const QorePythonClass* cls = static_cast<const QorePythonClass*>(meth.getClass());

    return cls->callPythonMethod(xsink, pypgm, mname->c_str(), args, pd, 2);
}

// static method
QoreValue QorePythonClass::memberGate(const QoreMethod& meth, void* m, QoreObject* self, QorePythonPrivateData* pd,
    const QoreListNode* args, q_rt_flags_t rtflags, ExceptionSink* xsink) {
    assert(args && args->size() == 2);
    assert(args->retrieveEntry(0).getType() == NT_STRING);
    assert(args->retrieveEntry(1).getType() == NT_BOOLEAN);

    const QoreStringNode* mname = args->retrieveEntry(0).get<QoreStringNode>();
    bool cls_access = args->retrieveEntry(1).getAsBool();

    QorePythonProgram* pypgm = QorePythonProgram::getPythonProgramFromMethod(meth, xsink);
    if (!pypgm) {
        assert(*xsink);
        return QoreValue();
    }

    const QorePythonClass* cls = static_cast<const QorePythonClass*>(meth.getClass());
    return cls->getPythonMember(pypgm, mname->c_str(), pd, xsink);
}

QoreValue QorePythonClass::callPythonMethod(ExceptionSink* xsink, QorePythonProgram* pypgm, const char* mname, const QoreListNode* args,
    QorePythonPrivateData* pd, size_t arg_offset) const {
    PyObject* pyobj = pd->get();
    PyTypeObject* mtype = Py_TYPE(pyobj);
    QorePythonHelper qph(pypgm);
    if (pypgm->checkValid(xsink)) {
        return QoreValue();
    }
    // returns a borrowed reference
    PyObject* attr = PyDict_GetItemString(mtype->tp_dict, mname);
    if (!attr) {
        xsink->raiseException("PYTHON-ERROR", "Python value of type '%s' has no method or member '%s'",
            mtype->tp_name, mname);
        return QoreValue();
    }
    return pypgm->callPythonMethod(xsink, attr, pyobj, args, 2);
}

QoreValue QorePythonClass::getPythonMember(QorePythonProgram* pypgm, const char* mname, QorePythonPrivateData* pd,
    ExceptionSink* xsink) const {
    QorePythonHelper qph(pypgm);
    if (pypgm->checkValid(xsink)) {
        return QoreValue();
    }

    {
        PyMemberDef* m = getPythonMember(mname);
        if (m) {
            return pypgm->getQoreValue(xsink, PyMember_GetOne((const char*)pd->get(), m));
        }
    }

    return pypgm->getQoreAttr(pd->get(), mname, xsink);
}
