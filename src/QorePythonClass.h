
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
#include "QorePythonPrivateData.h"

#include <string>
#include <vector>
#include <memory>
#include <map>

// forward references
class QorePythonProgram;

//! Represents a Python class in Qore
class QorePythonClass : public QoreBuiltinClass {
public:
    DLLLOCAL QorePythonClass(const char* name);

    DLLLOCAL QorePythonClass(const QorePythonClass& old) : QoreBuiltinClass(old) {
        doCopy(old);
    }

    DLLLOCAL virtual ~QorePythonClass() {
        for (auto& i : obj_sink) {
            Py_DECREF(i);
        }
    }

    //! Called when a class is copied for import
    DLLLOCAL virtual QoreClass* copyImport() {
        QorePythonClass* rv = new QorePythonClass;
        rv->doCopy(*this);
        return rv;
    }

    //! Called when a class is copied
    /** @since %Qore 0.9.5
    */
    DLLLOCAL virtual QoreClass* copy() {
        return new QorePythonClass(*this);
    }

    DLLLOCAL void addObj(PyObject* obj) {
        obj_sink.push_back(obj);
    }

    DLLLOCAL void addPythonMember(std::string member, PyMemberDef* memdef) {
        assert(mem_map.find(member) == mem_map.end());
        mem_map[member] = memdef;
    }

    DLLLOCAL PyMemberDef* getPythonMember(std::string member) const {
        mem_map_t::const_iterator i = mem_map.find(member);
        /*
        if (i == mem_map.end()) {
            for (auto& ix : mem_map) {
                printd(0, "MEM %s\n", ix.first.c_str());
            }
        }
        */
        return i == mem_map.end() ? nullptr : i->second;
    }

    DLLLOCAL QorePythonProgram* getPythonProgram() const {
        QoreProgram* qpgm = QoreClass::getProgram();
        return reinterpret_cast<QorePythonProgram*>(qpgm->getExternalData(QORE_PYTHON_MODULE_NAME));
    }

    DLLLOCAL QoreValue getPythonMember(QorePythonProgram* pypgm, const char* mname, QorePythonPrivateData* pd,
        ExceptionSink* xsink) const;

    DLLLOCAL QoreValue callPythonMethod(ExceptionSink* xsink, QorePythonProgram* pypgm, const char* mname, const QoreListNode* args,
        QorePythonPrivateData* pd, size_t arg_offset = 0) const;

    DLLLOCAL static QoreValue memberGate(const QoreMethod& meth, void* m, QoreObject* self, QorePythonPrivateData* pd,
        const QoreListNode* args, q_rt_flags_t rtflags, ExceptionSink* xsink);

    DLLLOCAL static QoreValue methodGate(const QoreMethod& meth, void* m, QoreObject* self, QorePythonPrivateData* pd,
        const QoreListNode* args, q_rt_flags_t rtflags, ExceptionSink* xsink);

private:
    // list of objects to dereference when the class is deleted
    typedef std::vector<PyObject*> obj_sink_t;
    obj_sink_t obj_sink;
    // map of builtin members: name -> member
    typedef std::map<std::string, PyMemberDef*> mem_map_t;
    mem_map_t mem_map;

    static type_vec_t gateParamTypeInfo;

    DLLLOCAL QorePythonClass() {
    }

    DLLLOCAL void doCopy(const QorePythonClass& old) {
        for (auto& i : old.obj_sink) {
            Py_INCREF(i);
            addObj(i);
        }
    }
};

#endif
