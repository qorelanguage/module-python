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

type_vec_t QorePythonClass::paramTypeInfo = { stringTypeInfo, boolTypeInfo };

// static method
QoreValue QorePythonClass::memberGate(const QoreMethod& meth, void* m, QoreObject* self, QorePythonPrivateData* pd,
    const QoreListNode* args, q_rt_flags_t rtflags, ExceptionSink* xsink) {
    assert(args && args->size() == 2);
    assert(args->retrieveEntry(0).getType() == NT_STRING);
    assert(args->retrieveEntry(1).getType() == NT_BOOLEAN);

    const QoreStringNode* mname = args->retrieveEntry(0).get<QoreStringNode>();
    bool cls_access = args->retrieveEntry(1).getAsBool();

    QoreProgram* pgm = self->getProgram();
    assert(pgm);
    QorePythonExternalProgramData* pypd = static_cast<QorePythonExternalProgramData*>(pgm->getExternalData("python"));
    assert(pypd);

    //return pypd->callMethod(xsink, meth.getClassName(), meth.getName(), args, 0, pd->get());
    xsink->raiseException("X", "x");
    return QoreValue();
}
