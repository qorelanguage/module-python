
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

class QorePythonClass : public QoreBuiltinClass {
public:
    DLLLOCAL QorePythonClass(const char* name) : QoreBuiltinClass(name, QDOM_UNCONTROLLED_API) {
        addMethod(nullptr, "memberGate", (q_external_method_t)memberGate, Public, 0, QDOM_UNCONTROLLED_API,
            autoTypeInfo, paramTypeInfo);

        setPublicMemberFlag();
        setGateAccessFlag();
    }

    DLLLOCAL const std::string& getJavaName() const {
        return jname;
    }

    static QoreValue memberGate(const QoreMethod& meth, void* m, QoreObject* self, QorePythonPrivateData* pd,
        const QoreListNode* args, q_rt_flags_t rtflags, ExceptionSink* xsink);

private:
    // java dot name for the class; ex: "java.lang.Object"
    std::string jname;

    static type_vec_t paramTypeInfo;
};

#endif
