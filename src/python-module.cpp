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
#include "QorePythonProgram.h"

static QoreStringNode* python_module_init();
static void python_module_ns_init(QoreNamespace* rns, QoreNamespace* qns);
static void python_module_delete();
static void python_module_parse_cmd(const QoreString& cmd, ExceptionSink* xsink);

// qore module symbols
DLLEXPORT char qore_module_name[] = QORE_PYTHON_MODULE_NAME;
DLLEXPORT char qore_module_version[] = PACKAGE_VERSION;
DLLEXPORT char qore_module_description[] = "python module";
DLLEXPORT char qore_module_author[] = "David Nichols";
DLLEXPORT char qore_module_url[] = "http://qore.org";
DLLEXPORT int qore_module_api_major = QORE_MODULE_API_MAJOR;
DLLEXPORT int qore_module_api_minor = QORE_MODULE_API_MINOR;
DLLEXPORT qore_module_init_t qore_module_init = python_module_init;
DLLEXPORT qore_module_ns_init_t qore_module_ns_init = python_module_ns_init;
DLLEXPORT qore_module_delete_t qore_module_delete = python_module_delete;
DLLEXPORT qore_module_parse_cmd_t qore_module_parse_cmd = python_module_parse_cmd;

DLLEXPORT qore_license_t qore_module_license = QL_MIT;
DLLEXPORT char qore_module_license_str[] = "MIT";

QoreNamespace PNS(QORE_PYTHON_NS_NAME);
static PyThreadState* mainThreadState = nullptr;

// module cmd type
using qore_python_module_cmd_t = void (*) (ExceptionSink* xsink, QoreString& arg, QorePythonProgram* pypgm);
static void py_mc_import(ExceptionSink* xsink, QoreString& arg, QorePythonProgram* pypgm);

// module cmds
typedef std::map<std::string, qore_python_module_cmd_t> mcmap_t;
static mcmap_t mcmap = {
    {"import", py_mc_import},
};

static QoreStringNode* python_module_init() {
    // initialize python library
    Py_Initialize();

    QorePythonProgram::staticInit();

    mainThreadState = PyThreadState_Get();
    PyEval_ReleaseLock();

    PNS.addSystemClass(initPythonProgramClass(PNS));

    return nullptr;
}

static void python_module_ns_init(QoreNamespace* rns, QoreNamespace* qns) {
    QoreProgram* pgm = getProgram();
    assert(pgm->getRootNS() == rns);
    if (!pgm->getExternalData(QORE_PYTHON_MODULE_NAME)) {
        QoreNamespace* pyns = PNS.copy();
        rns->addNamespace(pyns);
        pgm->setExternalData(QORE_PYTHON_MODULE_NAME, new QorePythonProgram(pgm, pyns));
    }
}

static void python_module_delete() {
    PyThreadState_Swap(nullptr);
    PyEval_AcquireThread(mainThreadState);
    Py_Finalize();
}

static void python_module_parse_cmd(const QoreString& cmd, ExceptionSink* xsink) {
    //printd(5, "python_module_parse_cmd() cmd: '%s'\n", cmd.c_str());

    const char* p = strchr(cmd.c_str(), ' ');

    if (!p) {
        xsink->raiseException("PYTHON-PARSE-COMMAND-ERROR", "missing command name in parse command: '%s'", cmd.c_str());
        return;
    }

    QoreString str(&cmd, p - cmd.c_str());

    QoreString arg(cmd);

    arg.replace(0, p - cmd.c_str() + 1, (const char*)0);
    arg.trim();

    mcmap_t::const_iterator i = mcmap.find(str.c_str());
    if (i == mcmap.end()) {
        QoreStringNode* desc = new QoreStringNodeMaker("unrecognized command '%s' in '%s' (valid commands: ", str.c_str(), cmd.c_str());
        for (mcmap_t::const_iterator i = mcmap.begin(), e = mcmap.end(); i != e; ++i) {
            if (i != mcmap.begin())
                desc->concat(", ");
            desc->sprintf("'%s'", i->first.c_str());
        }
        desc->concat(')');
        xsink->raiseException("PYTHON-PARSE-COMMAND-ERROR", desc);
        return;
    }

    QoreProgram* pgm = getProgram();
    QorePythonProgram* pypgm = static_cast<QorePythonProgram*>(pgm->getExternalData(QORE_PYTHON_MODULE_NAME));
    //printd(5, "parse-cmd '%s' pypgm: %p pythonns: %p\n", arg.c_str(), pypgm, pypgm->getPythonNamespace());
    if (!pypgm) {
        QoreNamespace* pyns = PNS.copy();
        pgm->getRootNS()->addNamespace(pyns);
        pgm->setExternalData(QORE_PYTHON_MODULE_NAME, new QorePythonProgram(pgm, pyns));
        pgm->addFeature(QORE_PYTHON_MODULE_NAME);
    }

    i->second(xsink, arg, pypgm);
}

static void py_mc_import(ExceptionSink* xsink, QoreString& arg, QorePythonProgram* pypgm) {
    // process import statement
    //printd(5, "python_module_parse_cmd() pypgm: %p arg: %s\n", pypgm, arg.c_str());

    // see if there is a dot (.) in the name
    qore_offset_t i = arg.find('.');
    if (i < 0 || i == static_cast<qore_offset_t>(arg.size() - 1)) {
        pypgm->import(xsink, arg.c_str());
    } else {
        const char* symbol = arg.c_str() + i + 1;
        arg.replaceChar(i, '\0');

        arg.terminate(i);
        if (!strcmp(symbol, "*")) {
            pypgm->import(xsink, arg.c_str());
        } else {
            pypgm->import(xsink, arg.c_str(), symbol);
        }
    }
}
