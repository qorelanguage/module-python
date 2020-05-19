/* -*- mode: c++; indent-tabs-mode: nil -*- */
/*
    QorePythonExternalProgramData.h

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

#ifndef _QORE_PYTHON_QOREPYTHONEXTERNALPROGRAMDATA_H

#define _QORE_PYTHON_QOREPYTHONEXTERNALPROGRAMDATA_H

#include "python-module.h"
#include "QorePythonProgram.h"

class QorePythonExternalProgramData : public AbstractQoreProgramExternalData, public QorePythonProgram {
public:
    DLLLOCAL QorePythonExternalProgramData(QoreNamespace* pyns) : QorePythonProgram(nullptr), pyns(pyns) {
    }

    DLLLOCAL QorePythonExternalProgramData(const QorePythonExternalProgramData& old, QoreProgram* pgm)
        : QorePythonProgram(nullptr), pyns(pgm->findNamespace("Python")) {
        if (!pyns) {
            pyns = PNS.copy();
            pgm->getRootNS()->addNamespace(pyns);
        }
    }

    DLLLOCAL QoreValue getQoreValue(PyObject* val, ExceptionSink* xsink);

    DLLLOCAL virtual AbstractQoreProgramExternalData* copy(QoreProgram* pgm) const {
        return new QorePythonExternalProgramData(*this, pgm);
    }

    DLLLOCAL virtual void doDeref() {
        delete this;
    }

    //! Static initialization
    DLLLOCAL static void staticInit();

    DLLLOCAL static QorePythonExternalProgramData* getContext() {
        QorePythonExternalProgramData* pypd;

        // first try to get the actual Program context
        QoreProgram* pgm = getProgram();
        if (pgm) {
            pypd = static_cast<QorePythonExternalProgramData*>(pgm->getExternalData("python"));
            if (pypd) {
                return pypd;
            }
        }
        pgm = qore_get_call_program_context();
        if (pgm) {
            pypd = static_cast<QorePythonExternalProgramData*>(pgm->getExternalData("python"));
            if (pypd) {
                return pypd;
            }
        }
        return nullptr;
    }

protected:
    QoreNamespace* pyns;
};

#endif
