/* indent-tabs-mode: nil -*- */
/*
  python Qore module

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

#include "python-module.h"
#include "QC_PythonProgram.h"

static QoreStringNode* python_module_init();
static void python_module_ns_init(QoreNamespace* rns, QoreNamespace* qns);
static void python_module_delete();

// qore module symbols
DLLEXPORT char qore_module_name[] = "python";
DLLEXPORT char qore_module_version[] = PACKAGE_VERSION;
DLLEXPORT char qore_module_description[] = "python module";
DLLEXPORT char qore_module_author[] = "David Nichols";
DLLEXPORT char qore_module_url[] = "http://qore.org";
DLLEXPORT int qore_module_api_major = QORE_MODULE_API_MAJOR;
DLLEXPORT int qore_module_api_minor = QORE_MODULE_API_MINOR;
DLLEXPORT qore_module_init_t qore_module_init = python_module_init;
DLLEXPORT qore_module_ns_init_t qore_module_ns_init = python_module_ns_init;
DLLEXPORT qore_module_delete_t qore_module_delete = python_module_delete;
DLLEXPORT qore_license_t qore_module_license = QL_MIT;
DLLEXPORT char qore_module_license_str[] = "MIT";

QoreNamespace PNS("Python");

static PyThreadState* mainThreadState = nullptr;

static QoreStringNode* python_module_init() {
    // initialize python library
    Py_Initialize();

    mainThreadState = PyThreadState_Get();
    PyEval_ReleaseLock();

    PNS.addSystemClass(initPythonProgramClass(PNS));

    return nullptr;
}

static void python_module_ns_init(QoreNamespace *rns, QoreNamespace *qns) {
    qns->addNamespace(PNS.copy());
}

static void python_module_delete() {
    PyThreadState_Swap(nullptr);
    PyEval_AcquireThread(mainThreadState);
    Py_Finalize();
}
