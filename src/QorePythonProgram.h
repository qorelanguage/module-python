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
#include <pythonrun.h>

class QorePythonProgram : public AbstractPrivateData {
public:
    DLLLOCAL QorePythonProgram(ExceptionSink* xsink) {
        //printd(5, "QorePythonProgram::QorePythonProgram() GIL thread state: %p\n", PyGILState_GetThisThreadState());
        QorePythonGilHelper qpgh;

        // save thread state to restore on exit
        PyThreadState* current_state = PyThreadState_Get();
        ON_BLOCK_EXIT(PyThreadState_Swap, current_state);

        //printd(5, "QorePythonProgram::QorePythonProgram() GIL thread state: %p\n", PyGILState_GetThisThreadState());
        if (createInterpreter(xsink)) {
            return;
        }
    }

    DLLLOCAL QorePythonProgram(const QoreString& source_code, const QoreString& source_label, int start, ExceptionSink* xsink) {
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
        _node* node = PyParser_SimpleParseString(src_code->c_str(), start);
        if (!node) {
            if (!checkPythonException(xsink)) {
                xsink->raiseException("PYTHON-COMPILE-ERROR", "parse failed");
            }
            return;
        }

        // compile parsed code
        python_code = (PyObject*)PyNode_Compile(node, src_label->c_str());
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
    }

    DLLLOCAL QoreValue run(ExceptionSink* xsink) {
        assert(python_code);
        QorePythonReferenceHolder return_value;
        {
            QorePythonHelper qph(python);
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

        QorePythonReferenceHolder python_code;
        {
            //printd(5, "QorePythonProgram::QorePythonProgram() GIL thread state: %p\n", PyGILState_GetThisThreadState());
            QorePythonGilHelper qpgh;

            // save thread state to restore on exit
            PyThreadState* current_state = PyThreadState_Get();
            ON_BLOCK_EXIT(PyThreadState_Swap, current_state);

            // parse code
            _node* node = PyParser_SimpleParseString(src_code->c_str(), input);
            if (!node) {
                if (!checkPythonException(xsink)) {
                    xsink->raiseException("PYTHON-COMPILE-ERROR", "parse failed");
                }
                return QoreValue();
            }

            // compile parsed code
            python_code = (PyObject*)PyNode_Compile(node, src_label->c_str());
            if (!python_code) {
                if (!checkPythonException(xsink)) {
                    xsink->raiseException("PYTHON-COMPILE-ERROR", "compile failed");
                }
                return QoreValue();
            }
        }

        QorePythonReferenceHolder return_value;
        {
            QorePythonHelper qph(python);
            PyObject* main = PyImport_AddModule("__main__");
            PyObject* main_dict = PyModule_GetDict(main);
            return_value = PyEval_EvalCode(*python_code, main_dict, main_dict);
        }

        // check for Python exceptions
        if (checkPythonException(xsink)) {
            return QoreValue();
        }

        return getQoreValue(return_value, xsink);
    }

    //! Call the function and return the result
    DLLLOCAL QoreValue callFunction(ExceptionSink* xsink, const QoreString& func_name, const QoreListNode* args, size_t arg_offset = 0);

    //! Call a method and return the result
    DLLLOCAL QoreValue callMethod(ExceptionSink* xsink, const QoreString& class_name, const QoreString& method_name, const QoreListNode* args, size_t arg_offset = 0);

    //! Returns a Qore value for the given Python value
    DLLLOCAL QoreValue getQoreValue(QorePythonReferenceHolder& val, ExceptionSink* xsink);

    //! Returns a Qore value for the given Python value; does not dereference val
    DLLLOCAL static QoreValue getQoreValue(PyObject* val, ExceptionSink* xsink);

    //! Returns a Qore list from a Python list
    DLLLOCAL static QoreListNode* getQoreListFromList(PyObject* val, ExceptionSink* xsink);

    //! Returns a Qore list from a Python tuple
    DLLLOCAL static QoreListNode* getQoreListFromTuple(PyObject* val, ExceptionSink* xsink);

    //! Returns a Qore hash from a Python dict
    DLLLOCAL static QoreHashNode* getQoreHashFromDict(PyObject* val, ExceptionSink* xsink);

    //! Returns a Qore binary from a Python Bytes object
    DLLLOCAL static BinaryNode* getQoreBinaryFromBytes(PyObject* val, ExceptionSink* xsink);

    //! Returns a Qore binary from a Python ByteArray object
    DLLLOCAL static BinaryNode* getQoreBinaryFromByteArray(PyObject* val, ExceptionSink* xsink);

    //! Returns a Qore relative date time value from a Python Delta object
    DLLLOCAL static DateTimeNode* getQoreDateTimeFromDelta(PyObject* val, ExceptionSink* xsink);

    //! Returns a Qore absolute date time value from a Python DateTime object
    DLLLOCAL static DateTimeNode* getQoreDateTimeFromDateTime(PyObject* val, ExceptionSink* xsink);

    //! Returns a Qore absolute date time value from a Python Date object
    DLLLOCAL static DateTimeNode* getQoreDateTimeFromDate(PyObject* val, ExceptionSink* xsink);

    //! Returns a Qore absolute date time value from a Python Time object
    DLLLOCAL static DateTimeNode* getQoreDateTimeFromTime(PyObject* val, ExceptionSink* xsink);

    //! Returns a Python list for the given Qore list
    DLLLOCAL static PyObject* getPythonList(ExceptionSink* xsink, const QoreListNode* l);

    //! Returns a Python tuple for the given Qore list
    DLLLOCAL static PyObject* getPythonTupleValue(ExceptionSink* xsink, const QoreListNode* l, size_t arg_offset = 0);

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

    //! Checks for a Python exception and creates a Qore exception from it
    DLLLOCAL static int checkPythonException(ExceptionSink* xsink);

    //! Returns a c string for the given python unicode value
    DLLLOCAL static const char* getCString(PyObject* obj) {
        Py_ssize_t size;
        return PyUnicode_AsUTF8AndSize(obj, &size);
    }

    //! Static initialization
    DLLLOCAL static void staticInit();

protected:
    PyThreadState* python = nullptr;
    QorePythonReferenceHolder module;
    QorePythonReferenceHolder python_code;
    PyObject* module_dict = nullptr;
    PyObject* builtin_dict = nullptr;

    DLLLOCAL virtual ~QorePythonProgram() {
        if (python) {
            QorePythonHelper qph(python);
            Py_EndInterpreter(python);
        }
    }

    //! the GIL must be held when this function is called
    DLLLOCAL int createInterpreter(ExceptionSink* xsink) {
        assert(PyGILState_Check());
        python = Py_NewInterpreter();
        if (!python) {
            xsink->raiseException("PYTHON-COMPILE-ERROR", "error creating the Pythong subinterpreter");
            return -1;
        }
        return 0;
    }
};

#endif