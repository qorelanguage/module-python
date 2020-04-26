/* -*- mode: c++; indent-tabs-mode: nil -*- */
/*
  QC_PythonProgram.h

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

#ifndef _QORE_CLASS_PYTHONPROGRAM

#define _QORE_CLASS_PYTHONPROGRAM

#include "python-module.h"

#include <pythonrun.h>

class QorePythonProgram : public AbstractPrivateData {
public:
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

        if (createInterpreter(xsink)) {
            return;
        }

        // parse code
        _node* node = PyParser_SimpleParseString(src_code->c_str(), start);
        if (!node) {
            xsink->raiseException("PYTHON-COMPILE-ERROR", "parse failed");
            return;
        }

        // compile parsed code
        python_code = (PyObject*)PyNode_Compile(node, src_label->c_str());
        if (!python_code) {
            xsink->raiseException("PYTHON-COMPILE-ERROR", "compile failed");
            return;
        }

        // create module for code
        module = PyImport_ExecCodeModule(src_label->c_str(), python_code);

        // returns a borrowed reference
        module_dict = PyModule_GetDict(module);
        assert(module_dict);

        /*
        PyObject *key, *value;
        Py_ssize_t pos = 0;

        while (PyDict_Next(module_dict, &pos, &key, &value)) {
            Py_ssize_t size;
            const char* keystr = PyUnicode_AsUTF8AndSize(key, &size);
            //const char* valstr = PyUnicode_AsUTF8AndSize(value, &size);
            printf("'%s' -> type '%s'\n", keystr ? keystr : "n/a", Py_TYPE(value)->tp_name);
        }
        */
    }

    DLLLOCAL QoreValue run(ExceptionSink* xsink) {
        assert(python_code);
        QorePythonReferenceHolder return_value;
        {
            QorePythonHelper qph(python);
            return_value = PyEval_EvalCode(python_code, module_dict, module_dict);
        }
        return getQoreValue(return_value.release(), xsink);
    }

    //! Call the function and return the result
    DLLLOCAL QoreValue callFunction(ExceptionSink* xsink, const QoreString& func_name, const QoreListNode* args, size_t arg_offset = 0);

    //! Returns a Qore value for the given Python value
    DLLLOCAL static QoreValue getQoreValue(PyObject* val, ExceptionSink* xsink);

    //! Returns a Python list for the given Qore list
    DLLLOCAL static PyObject* getPythonListValue(ExceptionSink* xsink, const QoreListNode* l, size_t arg_offset = 0);

    //! Returns a Python tuple for the given Qore list
    DLLLOCAL static PyObject* getPythonTupleValue(ExceptionSink* xsink, const QoreListNode* l, size_t arg_offset = 0);

    //! Returns a new reference
    DLLLOCAL static PyObject* getPythonValue(QoreValue val, ExceptionSink* xsink);

protected:
    PyThreadState* python = nullptr;
    PyObject* module = nullptr;
    PyObject* python_code = nullptr;
    PyObject* module_dict = nullptr;

    DLLLOCAL virtual ~QorePythonProgram() {
        if (python_code) {
            Py_DECREF(python_code);
        }
        if (module) {
            Py_DECREF(module);
        }
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

DLLLOCAL extern qore_classid_t CID_PYTHONPROGRAM;
DLLLOCAL extern QoreClass* QC_PYTHONPROGRAM;

DLLLOCAL QoreClass* initPythonProgramClass(QoreNamespace& ns);

#endif