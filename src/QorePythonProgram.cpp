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
#include "QoreLoader.h"
#include "PythonQoreClass.h"
#include "QoreMetaPathFinder.h"
#include "PythonCallableCallReferenceNode.h"
#include "PythonQoreCallable.h"

#include <structmember.h>
#include <frameobject.h>
#include <datetime.h>

#include <vector>
#include <string>

// type used for imported Qore namespaces in Python; a plan type with only a dictionary
struct PythonQoreNs {
    PyObject_HEAD
    PyObject* dict;
};

static strvec_t get_dot_path_list(const std::string str) {
    strvec_t rv;
    size_t start = 0;
    while (true) {
        size_t pos = str.find('.', start);
        rv.push_back(str.substr(start, pos - start));
        if (pos == std::string::npos) {
            break;
        }
        start = pos + 1;
    }
    return rv;
}

// from Python internal code
bool _qore_PyGILState_Check() {
    PyThreadState* tstate = _qore_PyRuntimeGILState_GetThreadState();
    if (tstate == NULL) {
        return false;
    }

    return (tstate == PyGILState_GetThisThreadState());
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
QorePythonProgram::py_global_tid_map_t QorePythonProgram::py_global_tid_map;
QoreThreadLock QorePythonProgram::py_thr_lck;

QorePythonProgram::QorePythonProgram() : save_object_callback(nullptr) {
    printd(5, "QorePythonProgram::QorePythonProgram() this: %p\n", this);
    assert(PyGILState_Check());
    PyThreadState* python;
    if (PyGILState_Check()) {
        assert(_qore_PyRuntimeGILState_GetThreadState() == PyGILState_GetThisThreadState());
        python = PyGILState_GetThisThreadState();
        interpreter = python->interp;
    } else {
        python = nullptr;
        interpreter = _PyGILState_GetInterpreterStateUnsafe();
    }
    owns_interpreter = false;

    createQoreProgram();

    // insert thread state into thread map
    AutoLocker al(py_thr_lck);
    assert(py_thr_map.find(this) == py_thr_map.end());
    int tid = gettid();
    py_thr_map[this] = {{tid, {python, false}}};
    py_global_tid_map[tid].insert(python);
}

QorePythonProgram::QorePythonProgram(QoreProgram* qpgm, QoreNamespace* pyns) : qpgm(qpgm), pyns(pyns), save_object_callback(nullptr) {
    printd(5, "QorePythonProgram::QorePythonProgram() this: %p GIL thread state: %p\n", this, PyGILState_GetThisThreadState());
    QorePythonGilHelper qpgh;

    //printd(5, "QorePythonProgram::QorePythonProgram() GIL thread state: %p\n", PyGILState_GetThisThreadState());
    if (createInterpreter(nullptr)) {
        valid = false;
    }

    // ensure that the __main__ module is created
    // returns a borrowed reference
    module = PyImport_AddModule("__main__");
    module.py_ref();

    ExceptionSink xsink;
    import(&xsink, "builtins");
    assert(!xsink);

    // returns a borrowed reference
    module_dict = PyModule_GetDict(*module);
    assert(module_dict);

    // returns a borrowed reference
    builtin_dict = PyDict_GetItemString(module_dict, "__builtins__");
    assert(builtin_dict);

    // import qoreloader module
    QorePythonReferenceHolder qoreloader(PyImport_ImportModule("qoreloader"));
    if (!qoreloader) {
        if (!checkPythonException(&xsink)) {
            xsink.raiseException("PYTHON-COMPILE-ERROR", "cannot load the 'qoreloader' module");
        }
        return;
    }

    PyDict_SetItemString(module_dict, "qoreloader", *qoreloader);
}

QorePythonProgram::QorePythonProgram(const QoreString& source_code, const QoreString& source_label, int start,
    ExceptionSink* xsink) : save_object_callback(nullptr) {
    printd(5, "QorePythonProgram::QorePythonProgram() this: %p\n", this);
    TempEncodingHelper src_code(source_code, QCS_UTF8, xsink);
    if (*xsink) {
        xsink->appendLastDescription(" (while processing the \"source_code\" argument)");
        return;
    }
    TempEncodingHelper src_label(source_label, QCS_UTF8, xsink);
    if (*xsink) {
        xsink->appendLastDescription(" (while processing the \"source_label\" argument)");
        return;
    }

    //printd(5, "QorePythonProgram::QorePythonProgram() GIL thread state: %p\n", PyGILState_GetThisThreadState());
    QorePythonGilHelper qpgh;

    //printd(5, "QorePythonProgram::QorePythonProgram() GIL thread state: %p\n", PyGILState_GetThisThreadState());
    if (createInterpreter(xsink)) {
        return;
    }

    // import qoreloader module
    QorePythonReferenceHolder qoreloader(PyImport_ImportModule("qoreloader"));
    if (!qoreloader) {
        if (!checkPythonException(xsink)) {
            xsink->raiseException("PYTHON-COMPILE-ERROR", "cannot load the 'qoreloader' module");
        }
        return;
    }
    //printd(5, "QorePythonProgram::QorePythonProgram() loaded qoreloader: %p\n", *qoreloader);

    // parse code
    QorePythonNodeHolder node(PyParser_SimpleParseString(src_code->c_str(), start));
    if (!node) {
        if (!checkPythonException(xsink)) {
            xsink->raiseException("PYTHON-COMPILE-ERROR", "parse failed");
        }
        return;
    }

    // compile parsed code
    python_code = (PyObject*)PyNode_Compile(*node, src_label->c_str());
    if (!python_code) {
        if (!checkPythonException(xsink)) {
            xsink->raiseException("PYTHON-COMPILE-ERROR", "compile failed");
        }
        return;
    }

    assert(!module);

    // create module for code
    QorePythonReferenceHolder new_module(PyImport_ExecCodeModule(src_label->c_str(), *python_code));
    if (!new_module) {
        if (!checkPythonException(xsink)) {
            xsink->raiseException("PYTHON-COMPILE-ERROR", "compile failed");
        }
        return;
    }

    module = new_module.release();

    // returns a borrowed reference
    module_dict = PyModule_GetDict(*module);
    assert(module_dict);

    // returns a borrowed reference
    builtin_dict = PyDict_GetItemString(module_dict, "__builtins__");
    assert(builtin_dict);

    PyDict_SetItemString(module_dict, "qoreloader", *qoreloader);

    // use the parent Program object as the source for importing
    qpgm = getProgram();
    owns_qore_program_ref = false;
    pyns = qpgm->findNamespace(QORE_PYTHON_NS_NAME);
    assert(pyns);
    //printd(5, "QorePythonProgram::QorePythonProgram() this: %p pgm: %p rootns: %p\n", this, qpgm, qpgm->getRootNS());
    // create Qore program object with the same restrictions as the parent
    //createQoreProgram();
}

void QorePythonProgram::createQoreProgram() {
    // create Qore program object with the same restrictions as the parent
    QoreProgram* pgm = getProgram();
    int64 parse_options = pgm ? pgm->getParseOptions64() : 0;
    qpgm = new QoreProgram(parse_options);
    owns_qore_program_ref = true;
    pyns = PNS.copy();
    qpgm->getRootNS()->addNamespace(pyns);
    qpgm->setExternalData(QORE_PYTHON_MODULE_NAME, this);

    //printd(5, "QorePythonProgram::createQoreProgram() this: %p pgm: %p rootns: %p\n", this, qpgm, qpgm->getRootNS());
}

QorePythonProgram* QorePythonProgram::getExecutionContext() {
    QorePythonProgram* pypgm = reinterpret_cast<QorePythonProgram*>(q_get_thread_local_data(python_u_tld_key));
    if (pypgm && pypgm->qpgm) {
        return pypgm;
    }
    //printd(5, "QorePythonProgram::getExecutionContext() current pypgm context %p has no Qore Program context\n", pypgm);
    return getContext();
}

QorePythonProgram* QorePythonProgram::getContext() {
    QorePythonProgram* pypgm;
    // first try to get the actual Program context
    QoreProgram* pgm = getProgram();
    if (pgm) {
        pypgm = static_cast<QorePythonProgram*>(pgm->getExternalData(QORE_PYTHON_MODULE_NAME));
        if (pypgm) {
            //printd(5, "QorePythonProgram::getContext() got local program context pgm: %p pypgm: %p\n", pgm, pypgm);
            return pypgm;
        }
    }
    pgm = qore_get_call_program_context();
    if (pgm) {
        pypgm = static_cast<QorePythonProgram*>(pgm->getExternalData(QORE_PYTHON_MODULE_NAME));
        if (pypgm) {
            //printd(5, "QorePythonProgram::getContext() got local call context pgm: %p pypgm: %p\n", pgm, pypgm);
            return pypgm;
        }
    }
    //printd(5, "QorePythonProgram::getContext() got global program context pypgm: %p\n", pypgm);
    return qore_python_pgm;
}

int QorePythonProgram::staticInit() {
    PyDateTime_IMPORT;
    return 0;
}

void QorePythonProgram::waitForThreadsIntern() {
    while (pgm_thr_cnt) {
        ++pgm_thr_waiting;
        pgm_thr_cond.wait(py_thr_lck);
        --pgm_thr_waiting;
    }
}

void QorePythonProgram::deleteIntern(ExceptionSink* xsink) {
    //printd(5, "QorePythonProgram::deleteIntern() this: %p\n", this);

    if (qpgm && owns_qore_program_ref) {
        // remove the external data before dereferencing
        qpgm->removeExternalData(QORE_PYTHON_MODULE_NAME);
        qpgm->waitForTerminationAndDeref(xsink);
    }
    qpgm = nullptr;

    // remove all thread states; the objects will be deleted by Python when the interpreter is destroyed
    {
        AutoLocker al(py_thr_lck);

        // wait for threads to complete before deleting entries
        waitForThreadsIntern();

        py_thr_map_t::iterator i = py_thr_map.find(this);
        assert(i != py_thr_map.end());
        //printd(5, "QorePythonProgram::deleteIntern() this: %p removing all thread states for pgm (delta: %d)\n", this, (int)i->second.size());
        for (auto& ti : i->second) {
            //printd(5, "QorePythonProgram::deleteIntern() this: %p removing TID %d\n", this, ti.first);
            py_global_tid_map_t::iterator gi = py_global_tid_map.find(ti.first);
            assert(gi != py_global_tid_map.end());
            py_thr_set_t::iterator thr_i = gi->second.find(ti.second.state);
            if (thr_i != gi->second.end()) {
                gi->second.erase(thr_i);
            }
        }
        py_thr_map.erase(i);
        assert(interpreter);
    }

    if ((interpreter && owns_interpreter)/* || module || python_code || !py_cls_map.empty() || !meth_vec.empty()*/) {
        {
            QorePythonHelper qph(this);

            for (auto& i : obj_sink) {
                Py_DECREF(i);
            }

            for (auto& i : meth_vec) {
                delete i;
            }
            meth_vec.clear();

            module.purge();
            python_code.purge();

            for (auto& i : py_cls_map) {
                delete i.second;
            }
            py_cls_map.clear();

            valid = false;
        }
        if (interpreter && owns_interpreter) {
            QorePythonGilHelper pgh;

            PyInterpreterState_Clear(interpreter);
            PyInterpreterState_Delete(interpreter);
            interpreter = nullptr;
            owns_interpreter = false;
        }
    }

    if (save_object_callback) {
        save_object_callback = nullptr;
    }

    //printd(5, "QorePythonProgram::deleteIntern() this: %p\n", this);
}

QoreValue QorePythonProgram::eval(ExceptionSink* xsink, const QoreString& source_code, const QoreString& source_label, int input, bool encapsulate) {
    TempEncodingHelper src_code(source_code, QCS_UTF8, xsink);
    if (*xsink) {
        xsink->appendLastDescription(" (while processing the \"source_code\" argument)");
        return QoreValue();
    }
    TempEncodingHelper src_label(source_label, QCS_UTF8, xsink);
    if (*xsink) {
        xsink->appendLastDescription(" (while processing the \"source_label\" argument)");
        return QoreValue();
    }

    // ensure atomic access to the Python interpreter (GIL) and manage the Python thread state
    QorePythonHelper qph(this);
    if (checkValid(xsink)) {
        return QoreValue();
    }

    QorePythonReferenceHolder return_value;
    QorePythonReferenceHolder python_code;
    {
        //printd(5, "QorePythonProgram::QorePythonProgram() GIL thread state: %p\n", PyGILState_GetThisThreadState());
        // parse code
        QorePythonNodeHolder node(PyParser_SimpleParseString(src_code->c_str(), input));
        if (!node) {
            if (!checkPythonException(xsink)) {
                xsink->raiseException("PYTHON-COMPILE-ERROR", "parse failed");
            }
            return QoreValue();
        }

        // compile parsed code
        python_code = (PyObject*)PyNode_Compile(*node, src_label->c_str());
        if (!python_code) {
            if (!checkPythonException(xsink)) {
                xsink->raiseException("PYTHON-COMPILE-ERROR", "compile failed");
            }
            return QoreValue();
        }
    }

    PyObject* main_dict;
    if (encapsulate) {
        // returns a borrowed reference
        PyObject* main = PyImport_AddModule("__main__");
        // returns a borrowed reference
        main_dict = PyModule_GetDict(main);
    } else {
        assert(module);
        // returns a borrowed reference
        main_dict = PyModule_GetDict(*module);
    }

    return_value = PyEval_EvalCode(*python_code, main_dict, main_dict);

    // check for Python exceptions
    if (checkPythonException(xsink)) {
        return QoreValue();
    }

    return getQoreValue(xsink, return_value);
}

void QorePythonProgram::pythonThreadCleanup(void*) {
    int tid = gettid();
    //printd(5, "QorePythonProgram::pythonThreadCleanup()\n");
    AutoLocker al(py_thr_lck);

    // delete all thread states for the tid
    for (auto& i : py_thr_map) {
        py_tid_map_t::iterator ti = i.second.find(tid);
        if (ti != i.second.end()) {
            //printd(5, "QorePythonProgram::pythonThreadCleanup() deleting state %p for TID %d", ti->second, tid);
            /*
            if (ti->second.owns_state) {
                printd(5, "QorePythonProgram::pythonThreadCleanup() deleting thread state %p for TID %d", ti->second, tid);
                // delete thread state
                QorePythonGilHelper pgh;
                PyThreadState_Clear(ti->second.state);
                PyThreadState_Delete(ti->second.state);
            }
            */
            i.second.erase(ti);
        }
    }

    // delete reverse lookups
    py_global_tid_map_t::iterator gi = py_global_tid_map.find(tid);
    if (gi != py_global_tid_map.end()) {
        //printd(5, "QorePythonProgram::pythonThreadCleanup() deleting global info for TID %d", tid);
        py_global_tid_map.erase(gi);
    }
    //printd(5, "QorePythonProgram::pythonThreadCleanup() done\n");
}

bool QorePythonProgram::haveGil() {
    if (!_qore_PyCeval_GetGilLockedStatus()) {
        return false;
    }

    PyThreadState* tstate = _qore_PyCeval_GetThreadState();
    if (!tstate) {
        return false;
    }

    int tid = gettid();
    AutoLocker al(py_thr_lck);

    py_global_tid_map_t::iterator i = py_global_tid_map.find(tid);
    if (i != py_global_tid_map.end()) {
        return i->second.find(tstate) != i->second.end();
    }
    return false;
}

bool QorePythonProgram::haveGil(PyThreadState* check_tstate) {
    if (!_qore_PyCeval_GetGilLockedStatus()) {
        return false;
    }

    PyThreadState* tstate = _qore_PyCeval_GetThreadState();
    return tstate == check_tstate;
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

    // NOTE: we have to reenable PyGILState_Check() here
    _QORE_PYTHON_REENABLE_GIL_CHECK

    _qore_PyGILState_SetThisThreadState(python);

    interpreter = python->interp;
    owns_interpreter = true;

    // save thread state
    int tid = gettid();
    AutoLocker al(py_thr_lck);
    {
        py_thr_map_t::iterator ti = py_thr_map.lower_bound(this);
        if (ti == py_thr_map.end() || ti->first != this) {
            py_thr_map.insert(ti, {this, {{tid, {python, true}}}});
        } else {
            ti->second[tid] = {python, true};
        }
    }
    {
        py_global_tid_map_t::iterator i = py_global_tid_map.lower_bound(tid);
        if (i == py_global_tid_map.end() || i->first != tid) {
            py_global_tid_map.insert(i, {tid, {python}});
        } else {
            i->second.insert(python);
        }
        //printd(5, "QorePythonProgram::createInterpreter() inserted TID %d -> %p\n", tid, python);
    }

    //printd(5, "QorePythonProgram::createInterpreter() this: %p\n", this);
    return 0;
}

QorePythonThreadInfo QorePythonProgram::setContext() const {
    if (!valid) {
        return {nullptr, nullptr, nullptr, PyGILState_UNLOCKED, false};
    }
    assert(interpreter);
    PyThreadState* python = getAcquireThreadState();
    // create new thread state if necessary
    if (!python) {
        python = PyThreadState_New(interpreter);
        //printd(5, "QorePythonProgram::setContext() this: %p created new thread context: %p (py_thr_map: %p size: %d)\n", this, python, &py_thr_map, (int)py_thr_map.size());
        assert(python);
        assert(python->gilstate_counter == 1);
        // the thread state will be deleted when the thread terminates or the interpreter is deleted
        int tid = gettid();
        AutoLocker al(py_thr_lck);
        {
            py_thr_map_t::iterator i = py_thr_map.find(this);
            if (i == py_thr_map.end()) {
                py_thr_map[this] = {{tid, {python, owns_interpreter}}};
            } else {
                assert(i->second.find(tid) == i->second.end());
                i->second[tid] = {python, owns_interpreter};
            }
            assert(py_thr_map.find(this) != py_thr_map.end());
        }
        {
            py_global_tid_map_t::iterator i = py_global_tid_map.lower_bound(tid);
            if (i == py_global_tid_map.end() || i->first != tid) {
                py_global_tid_map.insert(i, {tid, py_thr_set_t {python,}});
            } else {
                i->second.insert(python);
            }
            //printd(5, "QorePythonProgram::setContext() inserted TID %d -> %p\n", tid, python);
        }
        //printd(5, "QorePythonProgram::setContext() this: %p\n", this);
    }

    //printd(5, "QorePythonProgram::setContext() got thread context: %p (GIL: %d hG: %d) refs: %d\n", python, PyGILState_Check(), haveGil(), python->gilstate_counter);

    PyGILState_STATE g_state;
    // the TSS state needs to be restored in any case
    PyThreadState* tss_state = PyGILState_GetThisThreadState();
    PyThreadState* t_state, * ceval_state;

    // set new TSS thread state
    if (tss_state != python) {
        _qore_PyGILState_SetThisThreadState(python);
    }

    if (haveGil()) {
        // set GIL context
        ceval_state = _qore_PyCeval_SwapThreadState(python);

        g_state = PyGILState_LOCKED;
    } else {
        //assert(!_qore_PyRuntimeGILState_GetThreadState());
        //assert(!PyGILState_GetThisThreadState());


        ceval_state = nullptr;
        PyEval_RestoreThread(python);
        g_state = PyGILState_UNLOCKED;
    }

    t_state = _qore_PyRuntimeGILState_GetThreadState();

    if (t_state != python) {
        PyThreadState_Swap(python);
    }

    assert(PyGILState_Check());
    assert(haveGil(python));
    assert(_qore_PyCeval_GetThreadState() == python);
    assert(_qore_PyRuntimeGILState_GetThreadState() == python);
    // TSS state
    assert(PyGILState_GetThisThreadState() == python);

    //printd(5, "QorePythonProgram::setContext() old thread context: %p\n", t_state);

    ++python->gilstate_counter;

    return {tss_state, t_state, ceval_state, g_state, true};
}

void QorePythonProgram::releaseContext(const QorePythonThreadInfo& oldstate) const {
    if (!oldstate.valid) {
        return;
    }

    //struct _gilstate_runtime_state* gilstate = &_PyRuntime.gilstate;
    PyThreadState* python = getReleaseThreadState();
    assert(python);
    assert(_qore_PyThreadState_IsCurrent(python));

    /*
    // restore the old state
    if (oldstate.ceval_state != python) {
        // set GIL context
        _qore_PyCeval_SwapThreadState(oldstate.ceval_state);
    }
    */

    --python->gilstate_counter;

    //printd(5, "QorePythonProgram::releaseContext() t_state: %p g_state: %d\n", oldstate.t_state, oldstate.g_state);

    if (oldstate.g_state == PyGILState_UNLOCKED) {
        PyEval_ReleaseThread(python);
        assert(!PyGILState_Check());
        assert(!haveGil());
    } else {
        // restore old thread context; GIL still held
        assert(haveGil());
    }

    if (python != oldstate.t_state) {
        PyThreadState_Swap(oldstate.t_state);
    }
    // set new TSS thread state
    if (oldstate.tss_state != python) {
        _qore_PyGILState_SetThisThreadState(oldstate.tss_state);
    }
}

PythonQoreClass* QorePythonProgram::findCreatePythonClass(const QoreClass& cls, const char* mod_name) {
    printd(5, "QorePythonProgram::findCreatePythonClass() %s.%s\n", mod_name, cls.getName());

    py_cls_map_t::iterator i = py_cls_map.lower_bound(&cls);
    if (i != py_cls_map.end() && i->first == &cls) {
        printd(5, "QorePythonProgram::findCreatePythonClass() returning existing %s.%s\n", mod_name, cls.getName());
        return i->second;
    }

    std::unique_ptr<PythonQoreClass> py_cls(new PythonQoreClass(this, mod_name, cls));
    PyTypeObject* t = py_cls->getPythonType();
    printd(5, "QorePythonProgram::findCreatePythonClass() returning new %s.%s type: %p (%s)\n", mod_name, cls.getName(), t, t->tp_name);
    py_cls_map.insert(i, py_cls_map_t::value_type(&cls, py_cls.get()));
    return py_cls.release();
}

struct func_capsule_t {
    const QoreExternalFunction& func;
    QorePythonProgram* py_pgm;

    DLLLOCAL func_capsule_t(const QoreExternalFunction& func, QorePythonProgram* py_pgm) : func(func), py_pgm(py_pgm) {
    }
};

static void func_capsule_destructor(PyObject* func_capsule) {
    func_capsule_t* fc = reinterpret_cast<func_capsule_t*>(PyCapsule_GetPointer(func_capsule, nullptr));
    delete fc;
}

int QorePythonProgram::importQoreFunctionToPython(PyObject* mod, const QoreExternalFunction& func) {
    printd(5, "QorePythonProgram::importQoreFunctionToPython() %s()\n", func.getName());

    std::unique_ptr<func_capsule_t> fc(new func_capsule_t(func, this));

    QorePythonReferenceHolder capsule(PyCapsule_New((void*)fc.release(), nullptr, func_capsule_destructor));

    std::unique_ptr<PyMethodDef> funcdef(new PyMethodDef);
    funcdef->ml_name = func.getName();
    funcdef->ml_meth = callQoreFunction;
    funcdef->ml_flags = METH_VARARGS;
    funcdef->ml_doc = nullptr;

    meth_vec.push_back(funcdef.get());

    QorePythonReferenceHolder pyfunc(PyCFunction_New(funcdef.release(), *capsule));
    assert(pyfunc);
    if (PyObject_SetAttrString(mod, func.getName(), *pyfunc)) {
        assert(PyErr_Occurred());
        printd(5, "QorePythonProgram::importQoreFunctionToPython() error setting function %s in %p (%s)\n", func.getName(), mod, Py_TYPE(mod)->tp_name);
        return -1;
    }
    printd(5, "QorePythonProgram::importQoreFunctionToPython() ns: %p (%s) %s = %p\n", mod, Py_TYPE(mod)->tp_name, func.getName(), &func);
    return 0;
}

int QorePythonProgram::saveQoreObjectFromPython(const QoreValue& rv, ExceptionSink& xsink) {
    // save object in thread-local data if relevant
    if (rv.getType() != NT_OBJECT) {
        return 0;
    }

    //printd(5, "QorePythonProgram::saveQoreObjectFromPython() this: %p val: %s soc: %p\n", this, rv.getFullTypeName(), *save_object_callback);

    if (save_object_callback) {
        ReferenceHolder<QoreListNode> args(new QoreListNode(autoTypeInfo), &xsink);
        args->push(rv.refSelf(), &xsink);
        save_object_callback->execValue(*args, &xsink);
        if (xsink) {
            raisePythonException(xsink);
            return -1;
        }
        return 0;
    }

    return saveQoreObjectFromPythonDefault(rv, xsink);
}

int QorePythonProgram::saveQoreObjectFromPythonDefault(const QoreValue& rv, ExceptionSink& xsink) {
    QoreHashNode* data = qpgm->getThreadData();
    assert(data);
    const char* domain_name;
    // get key name where to save the data if possible
    QoreValue v = data->getKeyValue("_python_save");
    if (v.getType() != NT_STRING) {
        domain_name = "_python_save";
    } else {
        domain_name = v.get<const QoreStringNode>()->c_str();
    }

    QoreValue kv = data->getKeyValue(domain_name);
    // ignore operation if domain exists but is not a list
    if (!kv || kv.getType() == NT_LIST) {
        QoreListNode* list;
        ReferenceHolder<QoreListNode> list_holder(&xsink);
        if (!kv) {
            // we need to assign list in data *after* we prepend the object to the list
            // in order to manage object counts
            list = new QoreListNode(autoTypeInfo);
            list_holder = list;
        } else {
            list = kv.get<QoreListNode>();
        }

        // prepend to list to ensure FILO destruction order
        list->splice(0, 0, rv, &xsink);
        if (!xsink && list_holder) {
             data->setKeyValue(domain_name, list_holder.release(), &xsink);
        }
        if (xsink) {
            raisePythonException(xsink);
            return -1;
        }
        //printd(5, "saveQoreObjectFromPythonDefault() domain: '%s' obj: %p %s\n", domain_name, rv.get<QoreObject>(), rv.get<QoreObject>()->getClassName());
    } else {
        //printd(5, "saveQoreObjectFromPythonDefault() NOT SAVING domain: '%s' HAS KEY v: %s (kv: %s)\n", domain_name, rv.getFullTypeName(), kv.getFullTypeName());
    }
    return 0;
}

void QorePythonProgram::raisePythonException(ExceptionSink& xsink) {
    QoreValue err(xsink.getExceptionErr());
    QoreValue desc(xsink.getExceptionDesc());
    QoreValue arg(xsink.getExceptionArg());

    ExceptionSink xsink2;
    QorePythonReferenceHolder tuple(PyTuple_New(arg ? 3 : 2));
    QorePythonReferenceHolder ex_arg(getPythonValue(err, &xsink2));
    if (ex_arg) {
        PyTuple_SET_ITEM(*tuple, 0, ex_arg.release());
    } else {
        Py_INCREF(Py_None);
        PyTuple_SET_ITEM(*tuple, 0, Py_None);
    }
    ex_arg = getPythonValue(desc, &xsink2);
    if (ex_arg) {
        PyTuple_SET_ITEM(*tuple, 1, ex_arg.release());
    } else {
        Py_INCREF(Py_None);
        PyTuple_SET_ITEM(*tuple, 1, Py_None);
    }
    if (arg) {
        ex_arg = getPythonValue(arg, &xsink2);
        if (ex_arg) {
            PyTuple_SET_ITEM(*tuple, 2, ex_arg.release());
        } else {
            Py_INCREF(Py_None);
            PyTuple_SET_ITEM(*tuple, 2, Py_None);
        }
    }
    xsink.clear();

    ex_arg = PyObject_CallObject((PyObject*)&PythonQoreException_Type, *tuple);
    //printd(5, "QorePythonProgram::raisePythonException() py_ex: %p %s\n", *ex_arg, Py_TYPE(*ex_arg)->tp_name);
    PyErr_SetObject((PyObject*)&PythonQoreException_Type, *ex_arg);
}

// import Qore definitions to Python
void QorePythonProgram::importQoreToPython(PyObject* mod, const QoreNamespace& ns, const char* mod_name) {
    //printd(5, "QorePythonProgram::importQoreToPython() mod: %p (%d) ns: %p '%s' mod name: '%s'\n", mod, Py_REFCNT(mod), &ns, ns.getName(), mod_name);

    // import all functions
    QoreNamespaceFunctionIterator fi(ns);
    while (fi.next()) {
        const QoreExternalFunction& func = fi.get();
        // do not import deprecated functions
        if (func.getCodeFlags() & QCF_DEPRECATED) {
            continue;
        }
        if (importQoreFunctionToPython(mod, func)) {
            return;
        }
    }

    // import all constants
    QoreNamespaceConstantIterator consti(ns);
    while (consti.next()) {
        if (importQoreConstantToPython(mod, consti.get())) {
            return;
        }
    }

    // import all classes
    QoreNamespaceClassIterator clsi(ns);
    while (clsi.next()) {
        if (importQoreClassToPython(mod, clsi.get(), mod_name)) {
            return;
        }
    }

    // import all subnamespaces as modules
    QoreNamespaceNamespaceIterator ni(ns);
    while (ni.next()) {
        if (importQoreNamespaceToPython(mod, ni.get())) {
            return;
        }
    }
}

int QorePythonProgram::importQoreConstantToPython(PyObject* mod, const QoreExternalConstant& constant) {
    //printd(5, "QorePythonProgram::importQoreConstantToPython() %s\n", constant.getName());

    ExceptionSink xsink;
    ValueHolder qoreval(constant.getReferencedValue(), &xsink);
    if (!xsink) {
        QorePythonReferenceHolder val(qore_python_pgm->getPythonValue(*qoreval, &xsink));
        if (!xsink) {
            if (!PyObject_SetAttrString(mod, constant.getName(), *val)) {
                return 0;
            }
        }
    }
    raisePythonException(xsink);
    return -1;
}

int QorePythonProgram::importQoreClassToPython(PyObject* mod, const QoreClass& cls, const char* mod_name) {
    PyTypeObject* py_cls = findCreatePythonClass(cls, mod_name)->getPythonType();
    //printd(5, "QorePythonProgram::importQoreClassToPython() py_cls: %p ('%s') cn: '%s' mod_name: '%s'\n", py_cls, py_cls->tp_name, cls.getName(), mod_name);

    PyObject* clsobj = (PyObject*)py_cls;
    if (PyObject_SetAttrString(mod, cls.getName(), clsobj)) {
        return -1;
    }
    return 0;
}

int QorePythonProgram::importQoreNamespaceToPython(PyObject* mod, const QoreNamespace& ns) {
    //printd(5, "QorePythonProgram::importQoreNamespaceToPython() %s\n", ns.getName());
    assert(PyModule_Check(mod));

    QoreStringMaker nsname("%s.%s", PyModule_GetName(mod), ns.getName());

    // create a submodule
    QorePythonReferenceHolder new_mod(newModule(nsname.c_str(), &ns));
    printd(5, "QorePythonProgram::importQoreNamespaceToPython() (mod) created new module '%s'\n", nsname.c_str());
    importQoreToPython(*new_mod, ns, nsname.c_str());
    if (PyObject_SetAttrString(mod, ns.getName(), *new_mod)) {
        return -1;
    }
    return 0;
}

PyObject* QorePythonProgram::newModule(const char* name, const QoreNamespace* ns_pkg) {
    QorePythonReferenceHolder new_mod(PyModule_New(name));
    if (ns_pkg) {
        std::string nspath = ns_pkg->getPath();
        QorePythonReferenceHolder path(PyUnicode_FromStringAndSize(nspath.c_str(), nspath.size()));
        PyObject_SetAttrString(*new_mod, "__path__", *path);
    }
    saveModule(name, *new_mod);
    return new_mod.release();
}

PyObject* QorePythonProgram::newModule(const char* name, const char* path) {
    QorePythonReferenceHolder new_mod(PyModule_New(name));
    QorePythonReferenceHolder py_path(PyUnicode_FromStringAndSize(path, strlen(path)));
    PyObject_SetAttrString(*new_mod, "__path__", *py_path);
    saveModule(name, *new_mod);
    return new_mod.release();
}

void QorePythonProgram::importQoreNamespaceToPython(const QoreNamespace& ns, const QoreString& py_mod_path, ExceptionSink* xsink) {
    //printd(5, "QorePythonProgram::getCreateModule() '%s'\n", path);
    assert(!py_mod_path.empty());

    QorePythonHelper qph(this);
    if (checkValid(xsink)) {
        return;
    }

    assert(module);
    // start with the root module
    module.py_ref();
    QorePythonReferenceHolder mod(*module);

    strvec_t strpath = get_dot_path_list(py_mod_path.c_str());
    assert(!strpath.empty());
    QoreString nspath;
    for (const std::string& str : strpath) {
        bool store = nspath.empty();
        if (!store) {
            nspath.concat('.');
        }
        nspath.concat(str);

        //printd(5, "QorePythonProgram::importQoreNamespaceToPython() '%s' str: '%s' mod: %p ('%s')\n", py_mod_path.c_str(), str.c_str(), *mod, Py_TYPE(*mod)->tp_name);
        if (PyObject_HasAttrString(*mod, str.c_str())) {
            QorePythonReferenceHolder new_mod(PyObject_GetAttrString(*mod, str.c_str()));
            if (PyModule_Check(*new_mod) || PyDict_Check(*new_mod)) {
                mod = new_mod.release();
                continue;
            }
            // WARNING: any existing attribute will be replaced with the new module
        }

        QorePythonReferenceHolder new_mod(newModule(nspath.c_str(), &ns));
        //printd(5, "QorePythonProgram::importQoreNamespaceToPython() created new module '%s' (store: %d)\n", nspath.c_str(), store);
        assert(new_mod);
        if (PyObject_SetAttrString(*mod, str.c_str(), *new_mod)) {
            if (!checkPythonException(xsink)) {
                xsink->raiseException("IMPORT-NS-ERROR", "could not set element '%s' when creating path '%s'",
                    str.c_str(), py_mod_path.c_str());
            }
            return;
        }
        mod = new_mod.release();
    }
    printd(5, "QorePythonProgram::importQoreNamespaceToPython() %s => %p ('%s': %s) mr: %d\n", py_mod_path.c_str(), *mod, Py_TYPE(*mod)->tp_name, ns.getName(), Py_REFCNT(*mod));
    assert(mod);
    importQoreToPython(*mod, ns, strpath.back().c_str());
    checkPythonException(xsink);
}

void QorePythonProgram::aliasDefinition(const QoreString& source_path, const QoreString& target_path) {
    ExceptionSink xsink;
    QorePythonHelper qph(this);
    if (checkValid(&xsink)) {
        throw QoreXSinkException(xsink);
    }

    if (!module) {
        throw QoreStandardException("PYTHON-ALIAS-ERROR", "source path '%s' cannot be found as there is no code "
            "context", source_path.c_str());
    }

    // start with the root Dict
    module.py_ref();
    QorePythonReferenceHolder source_obj(*module);

    strvec_t strpath = get_dot_path_list(source_path.c_str());
    assert(!strpath.empty());
    for (size_t i = 0, e = strpath.size(); i < e; ++i) {
        const std::string& str = strpath[i];
        if (!PyObject_HasAttrString(*source_obj, str.c_str())) {
            throw QoreStandardException("PYTHON-ALIAS-ERROR", "source path '%s': element '%s' not found",
                source_path.c_str(), str.c_str());
        }
        source_obj = PyObject_GetAttrString(*source_obj, str.c_str());
        assert(source_obj);
    }

    // get / create target
    module.py_ref();
    QorePythonReferenceHolder obj(*module);
    strpath = get_dot_path_list(target_path.c_str());
    assert(!strpath.empty());
    QoreString nspath;
    for (size_t i = 0, e = strpath.size(); i < e; ++i) {
        const std::string& str = strpath[i];
        if (!nspath.empty()) {
            nspath.concat('.');
        }
        nspath.concat(str);

        //printd(5, "QorePythonProgram::aliasDefinition() '%s' str: '%s' obj: %p ('%s')\n", target_path.c_str(), str.c_str(), *obj, Py_TYPE(*obj)->tp_name);
        QorePythonReferenceHolder new_elem;
        if (i < (e - 1)) {
            if (PyObject_HasAttrString(*obj, str.c_str())) {
                obj = PyObject_GetAttrString(*obj, str.c_str());
                continue;
            }
            // create a new module, if the parent is a module, or a dictionary
            QoreStringMaker path("alias:%s", source_path.c_str());
            new_elem = newModule(nspath.c_str(), path.c_str());
            //printd(5, "QorePythonProgram::aliasDefinition() created module '%s'\n", nspath.c_str());
        } else {
            new_elem = source_obj.release();
            //printd(5, "QorePythonProgram::aliasDefinition() got elem %p %s (path %s)\n", *new_elem, Py_TYPE(*new_elem)->tp_name, nspath.c_str());
            // if aliasing a module, insert the new alias in sys.modules
            if (PyModule_Check(*new_elem)) {
                saveModule(nspath.c_str(), *new_elem);
            }
        }

        if (PyObject_SetAttrString(*obj, str.c_str(), *new_elem) < 0) {
            ExceptionSink xsink;
            if (!checkPythonException(&xsink)) {
                new QoreStandardException("PYTHON-ALIAS-ERROR", "could not set element '%s' when creating " \
                    "target path '%s' while aliasing source path '%s'", str.c_str(), target_path.c_str(),
                    source_path.c_str());
            }
            throw QoreXSinkException(xsink);
        }
        if (i == (e - 1)) {
            break;
        }
        obj = new_elem.release();
    }
}

void QorePythonProgram::exportClass(ExceptionSink* xsink, QoreString& arg) {
    QorePythonHelper qph(this);
    if (checkValid(xsink)) {
        return;
    }

    // start with the root module
    module.py_ref();
    QorePythonReferenceHolder obj(*module);

    strvec_t strpath = get_dot_path_list(arg.c_str());
    assert(!strpath.empty());
    for (const std::string& str : strpath) {
        //printd(5, "QorePythonProgram::exportClass() '%s' str: '%s' mod: %p ('%s')\n", arg.c_str(), str.c_str(), mod, Py_TYPE(mod)->tp_name);
        if (!PyObject_HasAttrString(*obj, str.c_str())) {
            xsink->raiseException("EXPORT-CLASS-ERROR", "could find component '%s' in path '%s'", str.c_str(), arg.c_str());
            return;
        }
        obj = PyObject_GetAttrString(*obj, str.c_str());
    }

    if (!PyType_Check(*obj)) {
        xsink->raiseException("EXPORT-CLASS-ERROR", "path '%s' is not a class; got type '%s' instead", arg.c_str(),
            Py_TYPE(*obj)->tp_name);
        return;
    }

    PyTypeObject* type = reinterpret_cast<PyTypeObject*>(*obj);
    clmap_t::iterator i = clmap.lower_bound(type);
    if (i != clmap.end() && i->first == type) {
        xsink->raiseException("EXPORT-CLASS-ERROR", "Qore class for Python path '%s' already exists", arg.c_str());
        return;
    }

    qore_offset_t ci = arg.rfind('.');

    //QoreExternalProgramContextHelper pch(xsink, qpgm);
    // grab current Program's parse lock before manipulating namespaces
    CurrentProgramRuntimeExternalParseContextHelper pch;

    QoreNamespace* ns = qpgm->getRootNS();

    if (ci > 0) {
        QoreString ns_str(arg);
        ns_str.terminate(ci);
        ns_str.replaceAll(".", "::");
        ns = ns->findCreateNamespacePathAll(ns_str.c_str());
    }

    addClassToNamespaceIntern(xsink, ns, (PyTypeObject*)*obj, strpath.back().c_str(), i);
}

void QorePythonProgram::addModulePath(ExceptionSink* xsink, QoreString& arg) {
    q_env_subst(arg);
    printd(5, "QorePythonProgram::addModulePath() this: %p arg: '%s'\n", this, arg.c_str());

    QorePythonHelper qph(this);
    if (checkValid(xsink)) {
        return;
    }

    QorePythonReferenceHolder mod(PyImport_ImportModule("sys"));
    if (!mod) {
        if (!checkPythonException(xsink)) {
            throw QoreStandardException("PYTHON-ERROR", "cannot load 'sys' module");
        }
        return;
    }

    QorePythonReferenceHolder path;

    if (!PyObject_HasAttrString(*mod, "path")) {
        path = PyList_New(0);
    } else {
        path = PyObject_GetAttrString(*mod, "path");
        if (!PyList_Check(*path)) {
            throw QoreStandardException("PYTHON-ERROR", "'sys.path' is not a list; got type '%s' instead", Py_TYPE(*path)->tp_name);
        }
    }
    QorePythonReferenceHolder item(PyUnicode_FromStringAndSize(arg.c_str(), arg.size()));
    PyList_Append(*path, *item);
}

void QorePythonProgram::exportFunction(ExceptionSink* xsink, QoreString& arg) {
    QorePythonHelper qph(this);
    if (checkValid(xsink)) {
        return;
    }

    // start with the root module
    module.py_ref();
    QorePythonReferenceHolder obj(*module);

    strvec_t strpath = get_dot_path_list(arg.c_str());
    assert(!strpath.empty());
    for (const std::string& str : strpath) {
        //printd(5, "QorePythonProgram::exportClass() '%s' str: '%s' mod: %p ('%s')\n", arg.c_str(), str.c_str(), mod, Py_TYPE(mod)->tp_name);
        if (!PyObject_HasAttrString(*obj, str.c_str())) {
            xsink->raiseException("EXPORT-FUNCTION-ERROR", "could find component '%s' in path '%s'", str.c_str(), arg.c_str());
            return;
        }
        obj = PyObject_GetAttrString(*obj, str.c_str());
    }

    q_external_func_t qore_func = nullptr;
    if (PyFunction_Check(*obj)) {
        qore_func = (q_external_func_t)QorePythonProgram::execPythonFunction;
    } else if (PyCFunction_Check(*obj)) {
        qore_func = (q_external_func_t)QorePythonProgram::execPythonCFunction;
    } else {
        xsink->raiseException("EXPORT-FUNCTION-ERROR", "path '%s' is not a function; got type '%s' instead", arg.c_str(),
            Py_TYPE(*obj)->tp_name);
        return;
    }

    flmap_t::iterator i = flmap.lower_bound(*obj);
    if (i != flmap.end() && i->first == *obj) {
        xsink->raiseException("EXPORT-FUNCTION-ERROR", "Qore function for Python path '%s' already exists", arg.c_str());
        return;
    }

    qore_offset_t ci = arg.rfind('.');

    //QoreExternalProgramContextHelper pch(xsink, qpgm);
    // grab current Program's parse lock before manipulating namespaces
    CurrentProgramRuntimeExternalParseContextHelper pch;

    QoreNamespace* ns = qpgm->getRootNS();

    if (ci > 0) {
        QoreString ns_str(arg);
        ns_str.terminate(ci);
        ns_str.replaceAll(".", "::");
        ns = ns->findCreateNamespacePathAll(ns_str.c_str());
    }

    const char* func_name = strpath.back().c_str();
    if (findCreateQoreFunction(*obj, func_name, qore_func)) {
        xsink->raiseException("EXPORT-FUNCTION-ERROR", "Qore function for Python path '%s' already exists", arg.c_str());
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

QoreListNode* QorePythonProgram::getQoreListFromTuple(ExceptionSink* xsink, PyObject* val, size_t offset,
    bool for_args) {
    pyobj_set_t rset;
    return getQoreListFromTuple(xsink, val, rset, offset);
}

QoreListNode* QorePythonProgram::getQoreListFromTuple(ExceptionSink* xsink, PyObject* val, pyobj_set_t& rset,
    size_t offset, bool for_args) {
    assert(PyTuple_Check(val));
    Py_ssize_t len = PyTuple_Size(val);
    if (for_args && !len) {
        return nullptr;
    }
    ReferenceHolder<QoreListNode> rv(new QoreListNode(autoTypeInfo), xsink);
    for (Py_ssize_t i = offset; i < len; ++i) {
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

ResolvedCallReferenceNode* QorePythonProgram::getQoreCallRefFromFunc(ExceptionSink* xsink, PyObject* val) {
    assert(PyFunction_Check(val));
    Py_INCREF(val);
    weakRef();
    return new PythonCallableCallReferenceNode(this, val);
}

ResolvedCallReferenceNode* QorePythonProgram::getQoreCallRefFromMethod(ExceptionSink* xsink, PyObject* val) {
    assert(PyMethod_Check(val));
    PyMethodObject* m = reinterpret_cast<PyMethodObject*>(val);
    Py_INCREF(m->im_func);
    Py_INCREF(m->im_self);
    weakRef();
    // NOTE: classmethods will have their "self" arg = the class / type
    return new PythonCallableCallReferenceNode(this, m->im_func, PyType_Check(m->im_self) ? nullptr : m->im_self);
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
    //printd(5, "QorePythonBase::getQoreValue() val: %p '%s'\n", val, Py_TYPE(val)->tp_name);
    if (!val || val == Py_None) {
        return QoreValue();
    }

    // if this is already a Qore object, then return it
    if (PyQoreObject_Check(val)) {
        PyQoreObject* pyobj = reinterpret_cast<PyQoreObject*>(val);
        return pyobj->qobj->refSelf();
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
        size_t len = strlen(longstr);
        if (len < 19 || (len == 19
            && ((!sign && strcmp(longstr, "9223372036854775807") <= 0)
                || (sign && strcmp(longstr, "-9223372036854775808") <= 0)))) {
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

    if (PyFunction_Check(val)) {
        return getQoreCallRefFromFunc(xsink, val);
    }

    if (PyMethod_Check(val)) {
        return getQoreCallRefFromMethod(xsink, val);
    }

    QoreClass* cls = getCreateQorePythonClass(xsink, type);
    if (!cls) {
        assert(*xsink);
        return QoreValue();
    }

    Py_INCREF(val);
    QoreObject* obj = new QoreObject(cls, qpgm, new QorePythonPrivateData(val));
    //printd(5, "QorePythonProgram::getQoreValue() obj: %p cls: %p '%s' id: %d\n", obj, cls, cls->getName(), cls->getID());
    return obj;
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

PyObject* QorePythonProgram::getPythonTupleValue(ExceptionSink* xsink, const QoreListNode* l, size_t arg_offset,
    PyObject* first) {
    //printd(5, "QorePythonProgram::getPythonTupleValue() l: %d first: %p\n", l ? (int)l->size() : 0, first);
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
        assert(val);
        PyDict_SetItem(*dict, *key, *val);
    }

    return dict.release();
}

PyObject* QorePythonProgram::getPythonString(ExceptionSink* xsink, const QoreString* str) {
    TempEncodingHelper py_str(str, QCS_UTF8, xsink);
    if (*xsink) {
        return nullptr;
    }
    return PyUnicode_FromStringAndSize(py_str->c_str(), py_str->size());
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

PyObject* QorePythonProgram::getPythonCallable(ExceptionSink* xsink, const ResolvedCallReferenceNode* call) {
    QorePythonImplicitQoreArgHelper qpiqoh((void*)call);
    return PyObject_CallObject((PyObject*)&PythonQoreCallable_Type, nullptr);
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

        case NT_RUNTIME_CLOSURE:
        case NT_FUNCREF: {
            return getPythonCallable(xsink, val.get<const ResolvedCallReferenceNode>());
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

            PythonQoreClass* py_cls = findCreatePythonClass(*o->getClass(), "qore");
            return py_cls->wrap(o);
        }
    }

    // ignore types that cannot be converted to a Python value and return None
    Py_INCREF(Py_None);
    return Py_None;
}

QoreValue QorePythonProgram::callFunction(ExceptionSink* xsink, const QoreString& func_name, const QoreListNode* args, size_t arg_offset) {
    assert(!haveGil());
    TempEncodingHelper fname(func_name, QCS_UTF8, xsink);
    if (*xsink) {
        xsink->appendLastDescription(" (while processing the \"func_name\" argument)");
        return QoreValue();
    }

    // set Qore program context for Qore APIs
    QoreExternalProgramContextHelper pch(xsink, qpgm);
    if (*xsink) {
        return QoreValue();
    }

    ValueHolder rv(xsink);
    {
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

        //printd(5, "QorePythonProgram::callFunction() this: %p %s()\n", this, func_name.c_str());
        //return callInternal(xsink, py_func, args, arg_offset);
        rv = callInternal(xsink, py_func, args, arg_offset);
    }
    assert(!haveGil());
    return rv.release();
}

QoreValue QorePythonProgram::callMethod(ExceptionSink* xsink, const QoreString& class_name,
    const QoreString& method_name, const QoreListNode* args, size_t arg_offset) {
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
    // set Qore program context for Qore APIs
    QoreExternalProgramContextHelper pch(xsink, qpgm);
    if (*xsink) {
        return QoreValue();
    }

    QorePythonHelper qph(this);
    if (checkValid(xsink)) {
        return QoreValue();
    }

    assert(module_dict);
    assert(builtin_dict);

    // returns a borrowed reference
    PyObject* py_class = PyDict_GetItemString(module_dict, cname);
    if (!py_class || !PyType_Check(py_class)) {
        // returns a borrowed reference
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
    // set Qore program context for Qore APIs
    QoreExternalProgramContextHelper pch(xsink, qpgm);
    if (*xsink) {
        return QoreValue();
    }

    QorePythonHelper qph(this);
    if (checkValid(xsink)) {
        return QoreValue();
    }
    //printd(5, "QorePythonProgram::callInternal() f: %p args: %d (%d) self: %p\n", callable, args ? (int)args->size() : 0, (int)arg_offset, first);
    QorePythonReferenceHolder rv(callPythonInternal(xsink, callable, args, arg_offset, first));
    return *xsink ? QoreValue() : getQoreValue(xsink, rv.release());
}

PyObject* QorePythonProgram::callPythonInternal(ExceptionSink* xsink, PyObject* callable, const QoreListNode* args,
    size_t arg_offset, PyObject* first, PyObject* kwargs) {
    QorePythonReferenceHolder py_args;

    py_args = getPythonTupleValue(xsink, args, arg_offset, first);
    if (*xsink) {
        return nullptr;
    }

    //printd(5, "QorePythonProgram::callPythonInternal(): this: %p valid: %d argcount: %d\n", this, valid, (args && args->size() > arg_offset) ? args->size() - arg_offset : 0);
    QorePythonReferenceHolder return_value(PyEval_CallObjectWithKeywords(callable, *py_args, kwargs));
    // check for Python exceptions
    if (!return_value && checkPythonException(xsink)) {
        return nullptr;
    }

    return return_value.release();
}

QoreValue QorePythonProgram::callFunctionObject(ExceptionSink* xsink, PyObject* func, const QoreListNode* args,
    size_t arg_offset, PyObject* first) {
    // set Qore program context for Qore APIs
    QoreExternalProgramContextHelper pch(xsink, qpgm);
    if (*xsink) {
        return QoreValue();
    }

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

void QorePythonProgram::clearPythonException() {
    QorePythonReferenceHolder ex_type, ex_value, traceback;
    PyErr_Fetch(ex_type.getRef(), ex_value.getRef(), traceback.getRef());
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
                callstack.add(CT_USER, filename, line, line, funcname, QORE_PYTHON_LANG_NAME);
            }
            frame = frame->f_back;
        }
        use_loc = true;
    } else {
        use_loc = false;
    }

    // check if it's a QoreException
    if (*ex_type == (PyObject*)&PythonQoreException_Type) {
        assert(PyObject_HasAttrString(*ex_value, "err"));
        QorePythonReferenceHolder pyval(PyObject_GetAttrString(*ex_value, "err"));
        ValueHolder err(getQoreValue(xsink, *pyval), xsink);
        assert(err->getType() == NT_STRING);
        if (!*xsink) {
            ValueHolder desc(xsink);
            if (PyObject_HasAttrString(*ex_value, "desc")) {
                pyval = PyObject_GetAttrString(*ex_value, "desc");
                desc = getQoreValue(xsink, *pyval);
            }
            if (!*xsink) {
                assert(!desc || desc->getType() == NT_STRING);
                ValueHolder arg(xsink);
                if (PyObject_HasAttrString(*ex_value, "arg")) {
                    pyval = PyObject_GetAttrString(*ex_value, "arg");
                    desc = getQoreValue(xsink, *pyval);
                }
                if (!*xsink) {
                    QoreStringValueHelper errstr(*err);
                    QoreStringNodeValueHelper descstr(*desc);
                    if (use_loc) {
                        xsink->raiseExceptionArg(loc.get(), errstr->c_str(), arg->refSelf(), descstr.getReferencedValue(),
                            callstack);
                    } else {
                        xsink->raiseExceptionArg(errstr->c_str(), arg->refSelf(), descstr.getReferencedValue(), callstack);
                    }
                    return -1;
                }
            }
        }
    }

    if (!*xsink) {
        // get full exception class name
        PyTypeObject* py_cls = Py_TYPE(*ex_value);
        QoreString ex_name(py_cls->tp_name);
        if (PyObject_HasAttrString(reinterpret_cast<PyObject*>(py_cls), "__module__")) {
            QorePythonReferenceHolder ex_mod(PyObject_GetAttrString(reinterpret_cast<PyObject*>(py_cls), "__module__"));
            if (PyUnicode_Check(*ex_mod)) {
                ex_name.prepend(".");
                ex_name.prepend(PyUnicode_AsUTF8(*ex_mod));
            }
        }

        // get description
        QorePythonReferenceHolder desc(PyObject_Str(*ex_value));
        ValueHolder qore_desc(getQoreValue(xsink, *desc), xsink);
        if (!*xsink) {
            QoreStringNodeValueHelper descstr(*qore_desc);
            if (use_loc) {
                // check if we have a Qore exception
                xsink->raiseExceptionArg(loc.get(), ex_name.c_str(), QoreValue(),
                    descstr.getReferencedValue(), callstack);
            } else {
                // check if we have a Qore exception
                xsink->raiseExceptionArg(ex_name.c_str(), QoreValue(), descstr.getReferencedValue(), callstack);
            }
            return -1;
        }
    }

    xsink->appendLastDescription(" (while trying to convert Python exception arguments to Qore)");

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

QoreClass* QorePythonProgram::getCreateQorePythonClass(ExceptionSink* xsink, PyTypeObject* type, int flags) {
    // grab current Program's parse lock before manipulating namespaces
    CurrentProgramRuntimeExternalParseContextHelper pch;
    if (!pch) {
        xsink->raiseException("PROGRAM-ERROR", "cannot process Python type '%s' in deleted Program object",
            type->tp_name);
        return nullptr;
    }

    return getCreateQorePythonClassIntern(xsink, type, nullptr, flags);
}

QoreNamespace* QorePythonProgram::getNamespaceForObject(PyObject* obj) {
    //printd(5, "QorePythonProgram::getNamespaceForObject() obj: %p (%s)\n", obj, Py_TYPE(obj)->tp_name);
    QoreString ns_path;

    // use the __name__ attribute to derive the namespace path if possible
    if (PyObject_HasAttrString(obj, "__name__")) {
        QorePythonReferenceHolder name(PyObject_GetAttrString(obj, "__name__"));
        const char* name_str = PyUnicode_AsUTF8(*name);
        //printd(5, "QorePythonProgram::getNamespaceForObject() obj %p __name__ '%s'\n", obj, name_str);
        const char* p = strrchr(name_str, '.');
        if (p) {
            ns_path = name_str;
            ns_path.terminate(p - name_str);
            ns_path.replaceAll(".", "::");
        }
    }

    // otherwise use "module_context", if not available, use the __module__ attribute, if available
    if (!module_context && ns_path.empty() && PyObject_HasAttrString(obj, "__module__")) {
        QorePythonReferenceHolder mod(PyObject_GetAttrString(obj, "__module__"));
        if (PyUnicode_Check(*mod)) {
            const char* mod_str = PyUnicode_AsUTF8(*mod);
            //printd(5, "QorePythonProgram::getNamespaceForObject() obj %p __module__ '%s'\n", obj, mod_str);
            ns_path = mod_str;
            ns_path.replaceAll(".", "::");
        }
    }

    if (ns_path.empty()) {
        //printd(5, "QorePythonProgram::getNamespaceForObject() obj %p (%s) -> ns: Python\n", obj, Py_TYPE(obj)->tp_name);
        if (!module_context) {
            return pyns;
        }
        ns_path = module_context;
    }

    //printd(5, "QorePythonProgram::getNamespaceForObject() obj %p (%s) -> ns: Python::%s\n", obj, Py_TYPE(obj)->tp_name, ns_path.c_str());
    return pyns->findCreateNamespacePathAll(ns_path.c_str());
}

QoreClass* QorePythonProgram::getCreateQorePythonClassIntern(ExceptionSink* xsink, PyTypeObject* type, const char* cname, int flags) {
    //printd(5, "QorePythonProgram::getCreateQorePythonClassIntern() class: '%s'\n", type->tp_name);
    // see if the Python type already represents a Qore class
    if (PyQoreObjectType_Check(type)) {
        printd(5, "QorePythonProgram::getCreateQorePythonClassIntern() class: '%s' is Qore\n", type->tp_name);
        return const_cast<QoreClass*>(PythonQoreClass::getQoreClass(type));
    }
    printd(5, "QorePythonProgram::getCreateQorePythonClassIntern() creating Qore class for Python class: '%s' \n", type->tp_name);

    clmap_t::iterator i = clmap.lower_bound(type);
    if (i != clmap.end() && i->first == type) {
        return i->second;
    }

    // get relative path to class and class name
    std::string rpath_str;
    if (!cname) {
        const char* p = strrchr(type->tp_name, '.');
        if (p) {
            rpath_str = std::string(type->tp_name, p - type->tp_name);
            cname = p + 1;
        } else {
            cname = type->tp_name;
        }
    }

    // create new QorePythonClass
    QoreNamespace* ns = getNamespaceForObject(reinterpret_cast<PyObject*>(type));
    return addClassToNamespaceIntern(xsink, ns, type, cname, i, flags);
}

QorePythonClass* QorePythonProgram::addClassToNamespaceIntern(ExceptionSink* xsink, QoreNamespace* ns, PyTypeObject* type, const char* cname, clmap_t::iterator i, int flags) {
    // get a unique name for the class
    QoreString cname_str = cname;
    {
        int base = 0;
        while (ns->findLocalClass(cname_str.c_str())) {
            cname_str.clear();
            cname_str.sprintf("%s_base_%d", cname, base++);
        }
    }
    cname = cname_str.c_str();

    // create new class
    std::unique_ptr<QorePythonClass> cls(new QorePythonClass(this, cname));

    // insert into map
    clmap.insert(i, clmap_t::value_type(type, cls.get()));

    //printd(5, "QorePythonProgram::addClassToNamespaceIntern() ns: '%s' cls: '%s' (%s)\n", ns->getName(), cls->getName(), type->tp_name);

    return setupQorePythonClass(xsink, ns, type, cls, flags);
}

static constexpr int static_meth_flags = QCF_USES_EXTRA_ARGS;
static constexpr int normal_meth_flags = static_meth_flags | QCF_ABSTRACT_OVERRIDE_ALL;

QorePythonClass* QorePythonProgram::setupQorePythonClass(ExceptionSink* xsink, QoreNamespace* ns, PyTypeObject* type, std::unique_ptr<QorePythonClass>& cls, int flags) {
    //printd(5, "QorePythonProgram::setupQorePythonClass() ns: '%s' cls: '%s' (%s) flags: %d\n", ns->getName(), cls->getName(), type->tp_name, flags);
    cls->addConstructor((void*)type, (q_external_constructor_t)execPythonConstructor, Public,
            QCF_USES_EXTRA_ARGS, QDOM_UNCONTROLLED_API);
    cls->setDestructor((void*)type, (q_external_destructor_t)execPythonDestructor);

    // create Python mapping for QoreClass if necessary
    if (!PyQoreObjectType_Check(type)) {
        std::unique_ptr<PythonQoreClass> py_cls(new PythonQoreClass(this, type, *cls.get()));
        py_cls_map.insert(py_cls_map_t::value_type(cls.get(), py_cls.release()));
    }

    // add single base class
    if (type->tp_base) {
        QoreClass* bclass = getCreateQorePythonClassIntern(xsink, type->tp_base);
        if (!bclass) {
            assert(*xsink);
            return nullptr;
        }

        //printd(5, "QorePythonProgram::setupQorePythonClass() %s parent: %s (bclass: %p)\n", type->tp_name, type->tp_base->tp_name, bclass);
        cls->addBaseClass(bclass, true);
    }

    cls->addBuiltinVirtualBaseClass(QC_PYTHONBASEOBJECT);

    ns->addSystemClass(cls.get());

    printd(5, "QorePythonProgram::setupQorePythonClass() %s methods: %p\n", type->tp_name,
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
                // returns a new reference
                PyObject* py_method = PyStaticMethod_Type.tp_descr_get(value, nullptr, nullptr);
                assert(py_method);
                cls->addObj(py_method);
                cls->addStaticMethod((void*)py_method, keystr,
                    (q_external_static_method_t)QorePythonProgram::execPythonStaticMethod, Public, static_meth_flags,
                    QDOM_UNCONTROLLED_API, autoTypeInfo);
                printd(5, "QorePythonProgram::setupQorePythonClass() added static method " \
                    "%s.%s() (%s)\n", type->tp_name, keystr, Py_TYPE(value)->tp_name);
                continue;
            }
            // check for wrapper descriptors -> normal method
            if (var_type == &PyWrapperDescr_Type) {
                Py_INCREF(value);
                cls->addObj(value);
                if (!strcmp(keystr, "copy")) {
                    keystr = "_copy";
                }
                cls->addMethod((void*)value, keystr,
                    (q_external_method_t)QorePythonProgram::execPythonNormalWrapperDescriptorMethod, Public,
                    normal_meth_flags, QDOM_UNCONTROLLED_API, autoTypeInfo);
                printd(5, "QorePythonProgram::setupQorePythonClass() added normal wrapper " \
                    "descriptor method %s.%s() (%s) %p: %d\n", type->tp_name, keystr, Py_TYPE(value)->tp_name, value,
                    value->ob_refcnt);
                continue;
            }
            // check for method descriptors -> normal method
            if (var_type == &PyMethodDescr_Type) {
                Py_INCREF(value);
                cls->addObj(value);
                if (!strcmp(keystr, "copy")) {
                    keystr = "_copy";
                }
                cls->addMethod((void*)value, keystr,
                    (q_external_method_t)QorePythonProgram::execPythonNormalMethodDescriptorMethod, Public,
                    normal_meth_flags, QDOM_UNCONTROLLED_API, autoTypeInfo);
                printd(5, "QorePythonProgram::setupQorePythonClass() added normal method " \
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
                    (q_external_method_t)QorePythonProgram::execPythonNormalClassMethodDescriptorMethod, Public,
                    normal_meth_flags, QDOM_UNCONTROLLED_API, autoTypeInfo);
                printd(5, "QorePythonProgram::setupQorePythonClass() added normal "
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
                    (q_external_method_t)QorePythonProgram::execPythonNormalMethod, Public, normal_meth_flags,
                    QDOM_UNCONTROLLED_API, autoTypeInfo);
                printd(5, "QorePythonProgram::setupQorePythonClass() added normal method " \
                    "%s.%s() (%s)\n", type->tp_name, keystr, Py_TYPE(value)->tp_name);
                continue;
            }
            // check for builtin functions -> static method
            if (PyCFunction_Check(value)) {
                // do not need to save reference here
                cls->addStaticMethod((void*)value, keystr,
                    (q_external_static_method_t)QorePythonProgram::execPythonStaticCFunctionMethod, Public,
                    static_meth_flags, QDOM_UNCONTROLLED_API, autoTypeInfo);
                printd(5, "QorePythonProgram::setupQorePythonClass() added static C function method " \
                    "%s.%s() (%s)\n", type->tp_name, keystr, Py_TYPE(value)->tp_name);
                continue;
            }
            // check for member descriptors
            if (var_type == &PyMemberDescr_Type) {
                cls->addPythonMember(keystr, reinterpret_cast<PyMemberDescrObject*>(value)->d_member);
                continue;
            }

            printd(5, "QorePythonProgram::setupQorePythonClass() %s: member '%s': %s\n", type->tp_name, keystr, Py_TYPE(value)->tp_name);
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
    // set Qore program context for Qore APIs
    QoreExternalProgramContextHelper pch(xsink, qpgm);
    if (*xsink) {
        return QoreValue();
    }

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
    // set Qore program context for Qore APIs
    QoreExternalProgramContextHelper pch(xsink, pypgm->qpgm);
    if (*xsink) {
        return;
    }

    QorePythonHelper qph(pypgm);
    if (pypgm->checkValid(xsink)) {
        return;
    }

    assert(PyType_Check(pycls));

    // save Qore object for any Python class that needs it
    QorePythonImplicitQoreArgHelper qpiqoh(self);
    QorePythonReferenceHolder pyobj(pypgm->callPythonInternal(xsink, pycls, args));
    if (*xsink) {
        return;
    }

    // check base class initialization


    self->setPrivate(meth.getClass()->getID(), new QorePythonPrivateData(pyobj.release()));
}

void QorePythonProgram::execPythonDestructor(const QorePythonClass& thisclass, PyObject* pycls, QoreObject* self,
    QorePythonPrivateData* pd, ExceptionSink* xsink) {
    QorePythonProgram* pypgm = thisclass.getPythonProgram();

    QorePythonHelper qph(pypgm);
    // FIXME: cannot delete objects after the python program has been destroyed
    if (pypgm->valid) {
        pd->deref(xsink);
    }
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

    /*
    // XXX DEBUG DELETEME
    QorePythonReferenceHolder objstr(PyObject_Repr(obj));
    printd(5, "QorePythonProgram::callMethodDescriptorMethod() obj: %s (%s)\n", PyUnicode_AsUTF8(*objstr), Py_TYPE(obj)->tp_name);
    */

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

int QorePythonProgram::saveModule(const char* name, PyObject* mod) {
    QorePythonReferenceHolder sys(PyImport_ImportModule("sys"));
    if (!sys) {
        return -1;
    }
    if (!PyObject_HasAttrString(*sys, "modules")) {
        return -1;
    }
    QorePythonReferenceHolder modules(PyObject_GetAttrString(*sys, "modules"));
    //printd(5, "QorePythonProgram::saveModule() modules: %p %s\n", *modules, Py_TYPE(*modules)->tp_name);
    if (!PyDict_Check(*modules)) {
        return -1;
    }
    return PyDict_SetItemString(*modules, name, mod);
}

int QorePythonProgram::import(ExceptionSink* xsink, const char* module, const char* symbol) {
    // make sure we don't already have this symbol
    // returns a borrowed reference
    QorePythonReferenceHolder mod(PyImport_ImportModule(module));
    if (!mod) {
        if (!checkPythonException(xsink)) {
            throw QoreStandardException("PYTHON-IMPORT-ERROR", "Python could not load module '%s'", module);
        }
        return -1;
    }

    // https://docs.python.org/3/reference/import.html:
    // any module that contains a __path__ attribute is considered a package
    //bool is_package = PyObject_HasAttrString(*mod, "__path__");

    QoreString ns_path(module);

    if (symbol) {
        QoreString sym(symbol);
        // find intermediate modules
        while (true) {
            qore_offset_t i = sym.find('.');
            if (i <= 0 || (size_t)i == (sym.size() - 1)) {
                break;
            }
            QoreString mod_name(&sym, i);

            if (!PyObject_HasAttrString(*mod, mod_name.c_str())) {
                throw QoreStandardException("PYTHON-IMPORT-ERROR", "submodule '%s' is not an attribute of '%s'",
                    mod_name.c_str(), module);
            }
            QorePythonReferenceHolder mod_val(PyObject_GetAttrString(*mod, mod_name.c_str()));
            assert(mod_val);
            if (!PyModule_Check(*mod_val)) {
                throw QoreStandardException("PYTHON-IMPORT-ERROR", "'%s' is not a submodule but rather has " \
                    "type '%s'", mod_name.c_str(), Py_TYPE(*mod_val)->tp_name);
            }

            mod = mod_val.release();

            ns_path.sprintf("::%s", mod_name.c_str());

            sym.splice(0, i + 1, xsink);
            if (*xsink) {
                return -1;
            }
            symbol = sym.c_str();
        }

        // if the module has already been imported, then ignore
        if (mod_set.find(*mod) != mod_set.end()) {
            return 0;
        }

        PythonModuleContextHelper mch(this, ns_path.c_str());
        return checkImportSymbol(xsink, sym.c_str(), *mod, PyObject_HasAttrString(*mod, "__path__"), symbol, IF_ALL,
            false);
    }

    return importModule(xsink, *mod, nullptr, module, IF_ALL);

    /*
    // first import all classes
    if (importModule(xsink, *mod, nullptr, module, IF_CLASS)) {
        assert(*xsink);
        return -1;
    }

    return importModule(xsink, *mod, nullptr, module, IF_OTHER);
    */
}

int QorePythonProgram::importModule(ExceptionSink* xsink, PyObject* mod, PyObject* globals, const char* module,
    int filter) {
    PythonModuleContextHelper mch(this, module);

    // if the module has already been imported, then ignore
    if (mod_set.find(mod) != mod_set.end()) {
        return 0;
    }
    mod_set.insert(mod);

    PyObject* main = PyImport_AddModule("__main__");
    assert(main);
    Py_INCREF(mod);
    if (PyModule_AddObject(main, module, mod) < 0) {
        Py_DECREF(mod);
        if (!checkPythonException(xsink)) {
            xsink->raiseException("PYTHON-IMPORT-ERROR", "module '%s' could not be added to the main module", module);
        }
        return -1;
    }

    // returns a borrowed reference
    PyObject* mod_dict = PyModule_GetDict(mod);
    if (!mod_dict) {
        // no dictionary; cannot import module
        throw QoreStandardException("PYTHON-IMPORT-ERROR", "module '%s' has no dictionary", module);
    }
    // https://docs.python.org/3/reference/import.html:
    // any module that contains a __path__ attribute is considered a package
    bool is_package = (bool)PyDict_GetItemString(mod_dict, "__path__");

    //printd(5, "QorePythonProgram::importModule() '%s' mod: %p (%d) pkg: %d (def: %p)\n", module, mod, filter, is_package, PyModule_GetDef(mod));

    // check the dictionary for __all__, giving a list of strings as public symbols
    // returns a borrowed reference
    {
        PyObject* all = PyDict_GetItemString(mod_dict, "__all__");
        if (all && PyTuple_Check(all)) {
            Py_ssize_t len = PyTuple_Size(all);
            for (Py_ssize_t i = 0; i < len; ++i) {
                // returns a borrowed reference
                PyObject* sv = PyTuple_GetItem(all, i);
                if (!sv || !PyUnicode_Check(sv)) {
                    throw QoreStandardException("PYTHON-IMPORT-ERROR", "module '%s' __all__ has an invalid " \
                        "element with type '%s'; expecting 'str'", module, sv ? Py_TYPE(sv)->tp_name : "null");
                }
                if (checkImportSymbol(xsink, module, mod, is_package, PyUnicode_AsUTF8(sv), filter, true)) {
                    return -1;
                }
            }
            printd(5, "QorePythonProgram::importModule() '%s' mod: %p (%d) pkg: %d imported __all__: %d\n", module, mod, filter, is_package, len);
            return 0;
        }
    }

    QorePythonReferenceHolder dir(PyObject_Dir(mod));
    if (dir && PyList_Check(*dir)) {
        Py_ssize_t len = PyList_Size(*dir);
        for (Py_ssize_t i = 0; i < len; ++i) {
            // returns a borrowed reference
            PyObject* sv = PyList_GetItem(*dir, i);
            if (!sv || !PyUnicode_Check(sv)) {
                throw QoreStandardException("PYTHON-IMPORT-ERROR", "module '%s' __all__ has an invalid " \
                    "element with type '%s'; expecting 'str'", module, sv ? Py_TYPE(sv)->tp_name : "null");
            }

            if (checkImportSymbol(xsink, module, mod, is_package, PyUnicode_AsUTF8(sv), filter, true)) {
                return -1;
            }
        }
        printd(5, "QorePythonProgram::importModule() '%s' mod: %p (%d) pkg: %d imported dir: %d\n", module, mod, filter, is_package, len);
        return 0;
    }

    throw QoreStandardException("PYTHON-IMPORT-ERROR", "module '%s' has no symbol directory", module);
}

int QorePythonProgram::checkImportSymbol(ExceptionSink* xsink, const char* module, PyObject* mod, bool is_package,
    const char* symbol, int filter, bool ignore_missing) {
    if (!PyObject_HasAttrString(mod, symbol)) {
        if (ignore_missing) {
            return 0;
        }
        throw QoreStandardException("PYTHON-IMPORT-ERROR", "module '%s' references unknown symbol '%s'", module,
            symbol);
    }
    QorePythonReferenceHolder value(PyObject_GetAttrString(mod, symbol));
    assert(value);

    bool is_class = PyType_Check(*value);
    if (is_class) {
        if (!(filter & IF_CLASS)) {
            return 0;
        }
    } else {
        bool is_module = PyModule_Check(*value);
        if (is_module) {
            if (!is_package) {
                return 0;
            }
        } else if (!(filter & IF_OTHER)) {
            return 0;
        }
    }

    return importSymbol(xsink, *value, module, symbol, filter);
}

int QorePythonProgram::findCreateQoreFunction(PyObject* value, const char* symbol, q_external_func_t func) {
    QoreNamespace* ns = getNamespaceForObject(value);
    if (!ns->findLocalFunction(symbol)) {
        // do not need to save reference here
        ns->addBuiltinVariant((void*)value, symbol, func, QCF_USES_EXTRA_ARGS, QDOM_UNCONTROLLED_API, autoTypeInfo);
        printd(5, "QorePythonProgram::findCreateQoreFunction() added function %s::%s() (%s)\n", ns->getName(), symbol, Py_TYPE(value)->tp_name);
        return 0;
    }
    return -1;
}

int QorePythonProgram::importSymbol(ExceptionSink* xsink, PyObject* value, const char* module,
    const char* symbol, int filter) {
    printd(5, "QorePythonProgram::importSymbol() %s.%s (type %s)\n", module, symbol, Py_TYPE(value)->tp_name);
    // check for builtin functions -> static method
    if (PyCFunction_Check(value)) {
        // ignore errors
        findCreateQoreFunction(value, symbol, (q_external_func_t)QorePythonProgram::execPythonCFunction);
        return 0;
    }

    if (PyFunction_Check(value)) {
        // ignore errors
        findCreateQoreFunction(value, symbol, (q_external_func_t)QorePythonProgram::execPythonFunction);
        return 0;
    }

    if (PyType_Check(value)) {
        //printd(5, "QorePythonProgram::importSymbol() class sym: '%s' -> '%s' (%p)\n", symbol, reinterpret_cast<PyTypeObject*>(value)->tp_name, value);
        QoreClass* cls = getCreateQorePythonClassIntern(xsink, reinterpret_cast<PyTypeObject*>(value));
        if (*xsink) {
            assert(!cls);
            return -1;
        }

        //printd(5, "QorePythonProgram::importSymbol() added class %s.%s (%s)\n", module, symbol, cls->getName());
        return 0;
    }

    if (PyModule_Check(value)) {
        QoreStringMaker sub_module("%s::%s", module, symbol);
        return importModule(xsink, value, nullptr, sub_module.c_str(), filter);
    }

    //printd(5, "QorePythonProgram::importSymbol() adding const %s.%s = '%s'\n", module, symbol, Py_TYPE(value)->tp_name);
    ValueHolder v(getQoreValue(xsink, value), xsink);
    if (*xsink) {
        return -1;
    }
    // skip empty values
    if (!v) {
        return 0;
    }
    const QoreTypeInfo* typeInfo = v->getFullTypeInfo();

    QoreNamespace* ns = pyns->findCreateNamespacePathAll(module_context);
    if (ns->findLocalConstant(symbol)) {
        return 0;
    }

    //std::string path = ns->getPath();
    //printd(5, "QorePythonProgram::importSymbol() adding const %s.%s = '%s' (ns: %s)\n", module_context, symbol, qore_type_get_name(typeInfo), path.c_str());
    ns->addConstant(symbol, v.release(), typeInfo);
    return 0;
}

// Python integration
PyObject* QorePythonProgram::callQoreFunction(PyObject* self, PyObject* args) {
    /*
    QorePythonReferenceHolder selfstr(PyObject_Repr(self));
    assert(PyUnicode_Check(*selfstr));
    QorePythonReferenceHolder argstr(PyObject_Repr(args));
    assert(PyUnicode_Check(*argstr));
    printd(5, "QorePythonProgram::callQoreFunction() self: %p (%s) args: %s\n", self, PyUnicode_AsUTF8(*selfstr), PyUnicode_AsUTF8(*argstr));
    */

    assert(PyCapsule_CheckExact(self));
    func_capsule_t* fc = reinterpret_cast<func_capsule_t*>(PyCapsule_GetPointer(self, nullptr));
    assert(&fc->func);
    assert(fc->py_pgm);

    // get Qore arguments
    ExceptionSink xsink;
    assert(PyTuple_Check(args));
    ReferenceHolder<QoreListNode> qargs(fc->py_pgm->getQoreListFromTuple(&xsink, args), &xsink);
    if (!xsink) {
        ValueHolder rv(fc->func.evalFunction(nullptr, *qargs, fc->py_pgm->getQoreProgram(), &xsink), &xsink);
        if (!xsink) {
            return fc->py_pgm->getPythonValue(*rv, &xsink);
        }
    }

    fc->py_pgm->raisePythonException(xsink);
    return nullptr;
}
