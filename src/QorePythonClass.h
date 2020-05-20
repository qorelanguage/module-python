
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

#ifndef _QORE_PYTHON_QOREPYTHONCLASS_H

#define _QORE_PYTHON_QOREPYTHONCLASS_H

#include "python-module.h"
#include "QorePythonExternalProgramData.h"
#include "QorePythonPrivateData.h"

#include <string>
#include <vector>
#include <memory>

// Maintains references to Python objects reference in the class definition when it was scanned
class QorePythonClassData : public AbstractQoreClassUserData {
public:
    DLLLOCAL virtual ~QorePythonClassData() {
        for (auto& i : obj_sink) {
            Py_DECREF(i);
        }
    }

    DLLLOCAL void addObj(PyObject* obj) {
        obj_sink.push_back(obj);
    }

    DLLLOCAL virtual QorePythonClassData* copy() const override {
        std::unique_ptr<QorePythonClassData> rv(new QorePythonClassData);
        for (auto& i : obj_sink) {
            Py_INCREF(i);
            rv->addObj(i);
        }
        return rv.release();
    }

    DLLLOCAL virtual void doDeref() override {
        delete this;
    }

private:
    // list of objects to dereference when the class is deleted
    typedef std::vector<PyObject*> obj_sink_t;
    obj_sink_t obj_sink;
};

//! Represents a Python class in Qore
class QorePythonClass : public QoreBuiltinClass {
public:
    DLLLOCAL QorePythonClass(const char* name);

    DLLLOCAL void addObj(PyObject* obj) {
        QorePythonClassData* user = getManagedUserData<QorePythonClassData>();
        if (!user) {
            user = new QorePythonClassData;
            setUserData(user);
        }
        user->addObj(obj);
    }

    DLLLOCAL static QoreValue memberGate(const QoreMethod& meth, void* m, QoreObject* self, QorePythonPrivateData* pd,
        const QoreListNode* args, q_rt_flags_t rtflags, ExceptionSink* xsink);

private:
    static type_vec_t memberGateParamTypeInfo;
};

#endif
