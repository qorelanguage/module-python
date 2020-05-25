/* -*- mode: c++; indent-tabs-mode: nil -*- */
/*
  QorePythonProgram.h

  Qore Programming Language

  Copyright (C) 2020 Qore Technologies, s.r.o.

  Permission is hereby granted, free of charge, to any person obtaining a
  copy of this software and associated documentation files (the "Software"),
  to deal in the Software without restriction, including without limitation
  the rights to use, copy, modify, merge, publish, distribute, sublicense,
  and/or sell copies of the Software, and to permit persons to whom the
  Software is furnished to do so, subject to the following conditions:

  The above copyright notice and this permission notice shall be included in
  all copies or substantial portions of the Software.

  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
  IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
  FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
  AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
  LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
  FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
  DEALINGS IN THE SOFTWARE.

  Note that the Qore library is released under a choice of three open-source
  licenses: MIT (as above), LGPL 2+, or GPL 2+; see README-LICENSE for more
  information.
*/

#ifndef _QORE_QOREPYTHONPROGRAM

#define _QORE_QOREPYTHONPROGRAM

#include "python-module.h"

#include "QorePythonClass.h"
#include "QorePythonPrivateData.h"

#include <pythonrun.h>

// forward reference
class QorePythonProgram;

class QorePythonProgram : public AbstractPrivateData, public AbstractQoreProgramExternalData {
    friend class PythonModuleContextHelper;
public:
    //! Default Qore Python context; does not own the QoreProgram reference
    DLLLOCAL QorePythonProgram(QoreProgram* qpgm, QoreNamespace* pyns) : qpgm(qpgm), pyns(pyns) {
        //printd(5, "QorePythonProgram::QorePythonProgram() GIL thread state: %p\n", PyGILState_GetThisThreadState());
        QorePythonGilHelper qpgh;

        // save thread state to restore on exit
        PyThreadState* current_state = PyThreadState_Get();
        ON_BLOCK_EXIT(PyThreadState_Swap, current_state);

        //printd(5, "QorePythonProgram::QorePythonProgram() GIL thread state: %p\n", PyGILState_GetThisThreadState());
        if (createInterpreter(nullptr)) {
            valid = false;
        }
    }

    //! New Qore Python context; does not own the QoreProgram reference
    DLLLOCAL QorePythonProgram(const QorePythonProgram& old, QoreProgram* qpgm) : QorePythonProgram(qpgm,
        qpgm->findNamespace(QORE_PYTHON_NS_NAME)) {
        if (!pyns) {
            pyns = PNS.copy();
            qpgm->getRootNS()->addNamespace(pyns);
        }
    }

    DLLLOCAL QorePythonProgram(const QoreString& source_code, const QoreString& source_label, int start,
        ExceptionSink* xsink) {
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

        // save thread state to restore on exit
        PyThreadState* current_state = PyThreadState_Get();
        ON_BLOCK_EXIT(PyThreadState_Swap, current_state);

        //printd(5, "QorePythonProgram::QorePythonProgram() GIL thread state: %p\n", PyGILState_GetThisThreadState());
        if (createInterpreter(xsink)) {
            return;
        }

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

        // create module for code
        module = PyImport_ExecCodeModule(src_label->c_str(), *python_code);

        // returns a borrowed reference
        module_dict = PyModule_GetDict(*module);
        assert(module_dict);

        // returns a borrowed reference
        builtin_dict = PyDict_GetItemString(module_dict, "__builtins__");
        assert(builtin_dict);

        // create Qore program object with the same restrictions as the parent
        QoreProgram* pgm = getProgram();
        int64 parse_options = pgm ? pgm->getParseOptions64() : 0;
        qpgm = new QoreProgram(parse_options);
        owns_qore_program_ref = true;
        pyns = PNS.copy();
        qpgm->getRootNS()->addNamespace(pyns);
        qpgm->setExternalData(QORE_PYTHON_MODULE_NAME, this);
    }

    DLLLOCAL virtual AbstractQoreProgramExternalData* copy(QoreProgram* pgm) const {
        return new QorePythonProgram(*this, pgm);
    }

    DLLLOCAL virtual void doDeref() {
        deref();
    }

    DLLLOCAL void destructor() {
        deleteIntern();
    }

    DLLLOCAL QoreValue run(ExceptionSink* xsink) {
        assert(python_code);
        QorePythonReferenceHolder return_value;
        {
            QorePythonHelper qph(python);
            if (checkValid(xsink)) {
                return QoreValue();
            }
            return_value = PyEval_EvalCode(*python_code, module_dict, module_dict);
        }

        // check for Python exceptions
        if (checkPythonException(xsink)) {
            return QoreValue();
        }

        return getQoreValue(return_value, xsink);
    }

    DLLLOCAL QoreValue eval(const QoreString& source_code, const QoreString& source_label, int input, ExceptionSink* xsink) {
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

        QorePythonReferenceHolder return_value;
        {
            // ensure atomic access to the Python interpreter (GIL) and manage the Python thread state
            QorePythonHelper qph(python);

            if (checkValid(xsink)) {
                return QoreValue();
            }

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

            {
                //QorePythonHelper qph(python);
                PyObject* main = PyImport_AddModule("__main__");
                PyObject* main_dict = PyModule_GetDict(main);
                return_value = PyEval_EvalCode(*python_code, main_dict, main_dict);
            }

            // check for Python exceptions
            if (checkPythonException(xsink)) {
                return QoreValue();
            }
        }

        return getQoreValue(return_value, xsink);
    }

    //! Call the function and return the result
    DLLLOCAL QoreValue callFunction(ExceptionSink* xsink, const QoreString& func_name, const QoreListNode* args,
        size_t arg_offset = 0);

    //! Call a method and return the result
    /** converts the string arguments to UTF-8 and makes the call
    */
    DLLLOCAL QoreValue callMethod(ExceptionSink* xsink, const QoreString& class_name, const QoreString& method_name,
        const QoreListNode* args, size_t arg_offset = 0);

    //! Call a method and return the result
    /** string args are assumed to be in UTF-8 encoding
    */
    DLLLOCAL QoreValue callMethod(ExceptionSink* xsink, const char* cname, const char* mname,
        const QoreListNode* args, size_t arg_offset = 0, PyObject* first = nullptr);

    //! Call a callable and and return the result
    DLLLOCAL QoreValue callInternal(ExceptionSink* xsink, PyObject* callable, const QoreListNode* args,
        size_t arg_offset = 0, PyObject* first = nullptr);

    //! Call a callable and and return the result as a Python value
    DLLLOCAL PyObject* callPythonInternal(ExceptionSink* xsink, PyObject* callable, const QoreListNode* args,
        size_t arg_offset = 0, PyObject* first = nullptr);

    //! Returns a Qore value for the given Python value; does not dereference val
    DLLLOCAL QoreValue getQoreValue(PyObject* val, ExceptionSink* xsink);

    //! Returns a Qore value for the given Python value
    DLLLOCAL QoreValue getQoreValue(QorePythonReferenceHolder& val, ExceptionSink* xsink);

    //! Returns the attribute of the given object as a Qore value
    DLLLOCAL QoreValue getQoreAttr(PyObject* obj, const char* attr, ExceptionSink* xsink);

    //! Sets the "save object callback" for %Qore objects created in Python code
    DLLLOCAL void setSaveObjectCallback(const ResolvedCallReferenceNode* save_object_callback) {
        if (this->save_object_callback) {
            this->save_object_callback->deref(nullptr);
        }
        this->save_object_callback = save_object_callback ? save_object_callback->refRefSelf() : nullptr;
    }

    //! Returns the "save object callback" for %Qore objects created in Python code
    DLLLOCAL ResolvedCallReferenceNode* getSaveObjectCallback() const {
        return save_object_callback;
    }

    //! Checks for a Python exception and creates a Qore exception from it
    DLLLOCAL int checkPythonException(ExceptionSink* xsink);

    //! Calls a Python method and returns the result as a %Qore value
    DLLLOCAL QoreValue callPythonMethod(ExceptionSink* xsink, PyObject* attr, PyObject* obj, const QoreListNode* args,
        size_t arg_offset = 0);

    //! Import Python code into the Qore program object
    DLLLOCAL int import(ExceptionSink* xsink, const char* module, const char* symbol = nullptr);

    //! Returns a Qore list from a Python list
    DLLLOCAL QoreListNode* getQoreListFromList(PyObject* val, ExceptionSink* xsink);

    //! Returns a Qore list from a Python tuple
    DLLLOCAL QoreListNode* getQoreListFromTuple(PyObject* val, ExceptionSink* xsink);

    //! Returns a Qore hash from a Python dict
    DLLLOCAL QoreHashNode* getQoreHashFromDict(PyObject* val, ExceptionSink* xsink);

    //! Returns a Qore binary from a Python Bytes object
    DLLLOCAL static BinaryNode* getQoreBinaryFromBytes(PyObject* val);

    //! Returns a Qore binary from a Python ByteArray object
    DLLLOCAL static BinaryNode* getQoreBinaryFromByteArray(PyObject* val);

    //! Returns a Qore relative date time value from a Python Delta object
    DLLLOCAL static DateTimeNode* getQoreDateTimeFromDelta(PyObject* val);

    //! Returns a Qore absolute date time value from a Python DateTime object
    DLLLOCAL static DateTimeNode* getQoreDateTimeFromDateTime(PyObject* val);

    //! Returns a Qore absolute date time value from a Python Date object
    DLLLOCAL static DateTimeNode* getQoreDateTimeFromDate(PyObject* val);

    //! Returns a Qore absolute date time value from a Python Time object
    DLLLOCAL static DateTimeNode* getQoreDateTimeFromTime(PyObject* val);

    //! Returns a Python list for the given Qore list
    DLLLOCAL static PyObject* getPythonList(ExceptionSink* xsink, const QoreListNode* l);

    //! Returns a Python tuple for the given Qore list
    DLLLOCAL static PyObject* getPythonTupleValue(ExceptionSink* xsink, const QoreListNode* l, size_t arg_offset = 0,
        PyObject* first = nullptr);

    //! Returns a Python dict for the given Qore hash
    DLLLOCAL static PyObject* getPythonDict(ExceptionSink* xsink, const QoreHashNode* h);

    //! Returns a Python string for the given Qore string
    DLLLOCAL static PyObject* getPythonString(ExceptionSink* xsink, const QoreString* str);

    //! Returns a Python string for the given Qore string
    DLLLOCAL static PyObject* getPythonByteArray(ExceptionSink* xsink, const BinaryNode* b);

    //! Returns a Python delta for the given Qore relative date/time value
    DLLLOCAL static PyObject* getPythonDelta(ExceptionSink* xsink, const DateTime* dt);

    //! Returns a Python string for the given Qore absolute date/time value
    DLLLOCAL static PyObject* getPythonDateTime(ExceptionSink* xsink, const DateTime* dt);

    //! Returns a new reference
    DLLLOCAL static PyObject* getPythonValue(QoreValue val, ExceptionSink* xsink);

    //! Returns a c string for the given python unicode value
    DLLLOCAL static const char* getCString(PyObject* obj) {
        assert(PyUnicode_Check(obj));
        return PyUnicode_AsUTF8(obj);
    }

    DLLLOCAL static QorePythonProgram* getPythonProgramFromMethod(const QoreMethod& meth, ExceptionSink* xsink) {
        const QoreClass* cls = meth.getClass();
        assert(dynamic_cast<const QorePythonClass*>(cls));
        QorePythonProgram* pypgm = static_cast<QorePythonProgram*>(cls->getProgram()->getExternalData(QORE_PYTHON_MODULE_NAME));
        if (!pypgm) {
            xsink->raiseException("PROGRAM-ERROR", "Python method %s::%s() cannot be executed because the Program " \
                "has been deleted", cls->getName(), meth.getName());
        }
        return pypgm;
    }

    DLLLOCAL static QorePythonProgram* getContext() {
        QorePythonProgram* pypgm;

        // first try to get the actual Program context
        QoreProgram* pgm = getProgram();
        if (pgm) {
            pypgm = static_cast<QorePythonProgram*>(pgm->getExternalData(QORE_PYTHON_MODULE_NAME));
            if (pypgm) {
                return pypgm;
            }
        }
        pgm = qore_get_call_program_context();
        if (pgm) {
            pypgm = static_cast<QorePythonProgram*>(pgm->getExternalData(QORE_PYTHON_MODULE_NAME));
            if (pypgm) {
                return pypgm;
            }
        }
        return nullptr;
    }

    //! Static initialization
    DLLLOCAL static void staticInit();

protected:
    PyThreadState* python = nullptr;
    QorePythonReferenceHolder module;
    QorePythonReferenceHolder python_code;
    PyObject* module_dict = nullptr;
    PyObject* builtin_dict = nullptr;
    //! each Python program object must have a corresponding Qore program object for Qore class generation
    QoreProgram* qpgm = nullptr;
    //! Python namespace ptr
    QoreNamespace* pyns = nullptr;
    //! module context when importing Python modules to Qore
    const char* module_context = nullptr;

    // call reference for saving object references
    ResolvedCallReferenceNode* save_object_callback = nullptr;

    //! set to true if this object owns the Qore program reference
    bool owns_qore_program_ref = false;
    //! true if the object is valid
    bool valid = true;

    //! maps types to classes
    typedef std::map<std::string, QorePythonClass*> clmap_t;
    clmap_t clmap;

    DLLLOCAL QorePythonClass* getCreateQorePythonClass(ExceptionSink* xsink, PyTypeObject* type);
    DLLLOCAL QorePythonClass* getCreateQorePythonClassIntern(ExceptionSink* xsink, PyTypeObject* type,
        QoreNamespace* parent_ns = nullptr);

    //! Call a method and and return the result
    DLLLOCAL QoreValue callCFunctionMethod(ExceptionSink* xsink, PyObject* func, const QoreListNode* args,
        size_t arg_offset = 0);

    //! Call a wrapper descriptor method and return the result
    DLLLOCAL QoreValue callWrapperDescriptorMethod(ExceptionSink* xsink, PyObject* self, PyObject* obj,
        const QoreListNode* args, size_t arg_offset = 0);
    //! Call a method descriptor method and return the result
    DLLLOCAL QoreValue callMethodDescriptorMethod(ExceptionSink* xsink, PyObject* self, PyObject* obj,
        const QoreListNode* args, size_t arg_offset = 0);
    //! Call a classmethod descriptor method and return the result
    DLLLOCAL QoreValue callClassMethodDescriptorMethod(ExceptionSink* xsink, PyObject* self, PyObject* obj,
        const QoreListNode* args, size_t arg_offset = 0);

    //! Import the given symbol into the Qore program object
    DLLLOCAL int importSymbol(ExceptionSink* xsink, PyObject* value, QoreNamespace* ns, const char* module,
        const char* symbol);

    DLLLOCAL static void execPythonConstructor(const QoreMethod& meth, PyObject* pycls, QoreObject* self,
        const QoreListNode* args, q_rt_flags_t rtflags, ExceptionSink* xsink);

    DLLLOCAL static QoreValue execPythonStaticMethod(const QoreMethod& meth, PyObject* m, const QoreListNode* args,
        q_rt_flags_t rtflags, ExceptionSink* xsink);
    DLLLOCAL static QoreValue execPythonNormalMethod(const QoreMethod& meth, PyObject* m, QoreObject* self,
        QorePythonPrivateData* pd, const QoreListNode* args, q_rt_flags_t rtflags, ExceptionSink* xsink);

    DLLLOCAL static QoreValue execPythonNormalWrapperDescriptorMethod(const QoreMethod& meth, PyObject* m,
        QoreObject* self, QorePythonPrivateData* pd, const QoreListNode* args, q_rt_flags_t rtflags,
        ExceptionSink* xsink);
    DLLLOCAL static QoreValue execPythonNormalMethodDescriptorMethod(const QoreMethod& meth, PyObject* m,
        QoreObject* self, QorePythonPrivateData* pd, const QoreListNode* args, q_rt_flags_t rtflags,
        ExceptionSink* xsink);
    DLLLOCAL static QoreValue execPythonNormalClassMethodDescriptorMethod(const QoreMethod& meth, PyObject* m,
        QoreObject* self, QorePythonPrivateData* pd, const QoreListNode* args, q_rt_flags_t rtflags,
        ExceptionSink* xsink);

    DLLLOCAL static QoreValue execPythonStaticCFunctionMethod(const QoreMethod& meth, PyObject* func,
        const QoreListNode* args, q_rt_flags_t rtflags, ExceptionSink* xsink);

    DLLLOCAL static QoreValue execPythonCFunction(PyObject* func, const QoreListNode* args, q_rt_flags_t rtflags,
        ExceptionSink* xsink);

    DLLLOCAL virtual ~QorePythonProgram() {
        deleteIntern();
    }

    DLLLOCAL void deleteIntern() {
        if (qpgm && owns_qore_program_ref) {
            // remove the external data before dereferencing
            qpgm->removeExternalData(QORE_PYTHON_MODULE_NAME);
            qpgm->deref(nullptr);
        }

        if (python) {
            QorePythonHelper qph(python);
            assert(valid);
            valid = false;

            Py_EndInterpreter(python);
            python = nullptr;
        }

        if (save_object_callback) {
            save_object_callback->deref(nullptr);
            save_object_callback = nullptr;
        }
    }

    //! the GIL must be held when this function is called
    DLLLOCAL int createInterpreter(ExceptionSink* xsink) {
        assert(PyGILState_Check());
        python = Py_NewInterpreter();
        if (!python) {
            if (xsink) {
                xsink->raiseException("PYTHON-COMPILE-ERROR", "error creating the Pythong subinterpreter");
            }
            return -1;
        }
        return 0;
    }

    //! Checks if the program is valid
    DLLLOCAL int checkValid(ExceptionSink* xsink) const {
        // the GIL must be held when this function is called
        assert(PyGILState_Check());
        if (!valid) {
            xsink->raiseException("PYTHON-ERROR", "the given PythonProgram object is invalid or has already been deleted");
            return -1;
        }
        return 0;
    }
};

class PythonModuleContextHelper {
public:
    DLLLOCAL PythonModuleContextHelper(QorePythonProgram* pypgm, const char* mod) : pypgm(pypgm) {
        assert(!pypgm->module_context);
        pypgm->module_context = mod;
    }

    DLLLOCAL ~PythonModuleContextHelper() {
        pypgm->module_context = nullptr;
    }

private:
    QorePythonProgram* pypgm;
};

#endif