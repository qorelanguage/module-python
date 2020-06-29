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

// module declaration for Qore 0.9.5+
void python_qore_module_desc(QoreModuleInfo& mod_info) {
    mod_info.name = QORE_PYTHON_MODULE_NAME;
    mod_info.version = PACKAGE_VERSION;
    mod_info.desc = "python module";
    mod_info.author = "David Nichols";
    mod_info.url = "http://qore.org";
    mod_info.api_major = QORE_MODULE_API_MAJOR;
    mod_info.api_minor = QORE_MODULE_API_MINOR;
    mod_info.init = python_module_init;
    mod_info.ns_init = python_module_ns_init;
    mod_info.del = python_module_delete;
    mod_info.parse_cmd = python_module_parse_cmd;
    mod_info.license = QL_MIT;
    mod_info.license_str = "MIT";
}

QoreNamespace PNS(QORE_PYTHON_NS_NAME);
PyThreadState* mainThreadState = nullptr;

QorePythonClass* QC_PYTHONBASEOBJECT;
qore_classid_t CID_PYTHONBASEOBJECT;

// module cmd type
using qore_python_module_cmd_t = void (*) (ExceptionSink* xsink, QoreString& arg, QorePythonProgram* pypgm);
static void py_mc_import(ExceptionSink* xsink, QoreString& arg, QorePythonProgram* pypgm);
static void py_mc_import_ns(ExceptionSink* xsink, QoreString& arg, QorePythonProgram* pypgm);
static void py_mc_alias(ExceptionSink* xsink, QoreString& arg, QorePythonProgram* pypgm);
static void py_mc_parse(ExceptionSink* xsink, QoreString& arg, QorePythonProgram* pypgm);
static void py_mc_export_class(ExceptionSink* xsink, QoreString& arg, QorePythonProgram* pypgm);

// module cmds
typedef std::map<std::string, qore_python_module_cmd_t> mcmap_t;
static mcmap_t mcmap = {
    {"import", py_mc_import},
    {"import-ns", py_mc_import_ns},
    {"alias", py_mc_alias},
    {"parse", py_mc_parse},
    {"export-class", py_mc_export_class}
};

static bool python_needs_shutdown = false;

#ifdef NEED_PYTHON_36_TLS_KEY
#ifndef __linux__
#error Python TLS key prediction required when linking with Python 3.6 only works on Linux
#endif
int autoTLSkey;
#endif

static void check_python_version() {
    QorePythonReferenceHolder mod(PyImport_ImportModule("sys"));
    if (!mod) {
        throw QoreStandardException("PYTHON-MODULE-ERROR", "Python could not load module 'sys'");
    }

    // returns a borrowed reference
    PyObject* mod_dict = PyModule_GetDict(*mod);
    if (!mod_dict) {
        throw QoreStandardException("PYTHON-MODULE-ERROR", "Python module 'sys' has no dictiomary");
    }

    // returns a borrowed reference
    PyObject* value = PyDict_GetItemString(mod_dict, "version_info");
    if (!value) {
        throw QoreStandardException("PYTHON-MODULE-ERROR", "symbol 'sys.version_info' not found; cannot verify the " \
            "runtime version of the Python library");
    }

    if (!PyObject_HasAttrString(value, "major")) {
        throw QoreStandardException("PYTHON-MODULE-ERROR", "symbol 'sys.version.major' was not found; cannot " \
            "verify the runtime version of the Python library");
    }

    QorePythonReferenceHolder py_major(PyObject_GetAttrString(value, "major"));
    if (!PyLong_Check(*py_major)) {
        throw QoreStandardException("PYTHON-MODULE-ERROR", "symbol 'sys.version.major' has type '%s'; expecting " \
            "'int'; cannot verify the runtime version of the Python library", Py_TYPE(*py_major)->tp_name);
    }

    long major = PyLong_AsLong(*py_major);
    if (major != PY_MAJOR_VERSION) {
        throw QoreStandardException("PYTHON-MODULE-ERROR", "Python runtime major version is %ld, but the module was " \
            "compiled with major version %d (%s)", major, PY_MAJOR_VERSION, PY_VERSION);
    }

    QorePythonReferenceHolder py_minor(PyObject_GetAttrString(value, "minor"));
    if (!PyLong_Check(*py_minor)) {
        throw QoreStandardException("PYTHON-MODULE-ERROR", "symbol 'sys.version.minor' has type '%s'; expecting " \
            "'int'; cannot verify the runtime version of the Python library", Py_TYPE(*py_minor)->tp_name);
    }

    long minor = PyLong_AsLong(*py_minor);
    if (minor != PY_MINOR_VERSION) {
        throw QoreStandardException("PYTHON-MODULE-ERROR", "Python runtime version is %ld.%ld, but the module was " \
            "compiled with version %d.%d (%s)", major, minor, PY_MAJOR_VERSION, PY_MINOR_VERSION, PY_VERSION);
    }

    //printd(5, "python runtime version OK: %ld.%ld.x =~ '%s'\n", major, minor, PY_VERSION);
}

static QoreStringNode* python_module_init() {
#ifdef NEED_PYTHON_36_TLS_KEY
    // Python 3.6 does not expose its thread-local key in the API, but we can determine the value by creating and
    // destroying a thread-local key before we call Py_Initialize()
    pthread_key_t k;
    pthread_key_create(&k, nullptr);
    //printd(5, "python_module_init() got Python 3.6 thread-local key: %d\n", k);
    autoTLSkey = k;
    pthread_key_delete(k);
#endif

    // initialize python library; do not register signal handlers
    if (!Py_IsInitialized()) {
        if (PyImport_AppendInittab("qoreloader", PyInit_qoreloader) == -1) {
            throw QoreStandardException("PYTHON-MODULE-ERROR", "cannot append the qoreloader module to Python");
        }

        Py_InitializeEx(0);
        python_needs_shutdown = true;
    } else {
#ifdef NEED_PYTHON_36_TLS_KEY
        throw QoreStandardException("PYTHON-MODULE-ERROR", "cannot use the \"python\" module when liked with Python " \
            "3.6 when not initialized by Qore");
#endif
    }

    // ensure that runtime version matches compiled version
    check_python_version();

    if (QorePythonProgram::staticInit()) {
        throw QoreStandardException("PYTHON-MODULE-ERROR", "failed to initialize \"python\" module");
    }

    mainThreadState = PyThreadState_Get();
    if (python_needs_shutdown) {
        // release the current thread state after initialization
        PyEval_ReleaseThread(mainThreadState);
        assert(!_qore_PyRuntimeGILState_GetThreadState());
        _qore_PyGILState_SetThisThreadState(nullptr);
        assert(!PyGILState_GetThisThreadState());
    }

    PNS.addSystemClass(initPythonProgramClass(PNS));

    tclist.push(QorePythonProgram::pythonThreadCleanup, nullptr);

    QC_PYTHONBASEOBJECT = new QorePythonClass("__qore_base__");
    CID_PYTHONBASEOBJECT = QC_PYTHONBASEOBJECT->getID();

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

    assert(!PyGILState_Check());
}

static void python_module_delete() {
    if (python_needs_shutdown) {
        PyThreadState_Swap(nullptr);
        PyEval_AcquireThread(mainThreadState);
        _qore_PyGILState_SetThisThreadState(mainThreadState);
        Py_Finalize();
    }
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

// %module-cmd(python) import
static void py_mc_import(ExceptionSink* xsink, QoreString& arg, QorePythonProgram* pypgm) {
    // process import statement
    //printd(5, "python_module_parse_cmd() pypgm: %p arg: %s\n", pypgm, arg.c_str());

    QorePythonHelper qph(pypgm);

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

// %module-cmd(python) import-ns <qore-namespace> <python-module-path>
static void py_mc_import_ns(ExceptionSink* xsink, QoreString& arg, QorePythonProgram* pypgm) {
    // find end of qore namespace
    qore_offset_t end = arg.find(' ');
    if (end == -1) {
        throw QoreStandardException("PYTHON-MODULE-ERROR", "syntax: import-ns <qore-namespace> " \
            "<python-module-path>: missing python module path argument; value given: '%s'", arg.c_str());
    }

    QoreString qore_ns(&arg, end);
    QoreString py_mod_path(arg.c_str() + end + 1);

    QoreProgram* pgm = getProgram();
    if (!pgm) {
        throw QoreStandardException("PYTHON-MODULE-ERROR", "import-ns error: no current Program context");
    }

    QoreNamespace* ns = pgm->findNamespace(qore_ns);
    if (!ns || ns == pgm->getRootNS()) {
        throw QoreStandardException("PYTHON-MODULE-ERROR", "import-ns error: Qore namespace '%s' not found",
            qore_ns.c_str());
    }

    pypgm->importQoreNamespaceToPython(*ns, py_mod_path, xsink);
}

// %module-cmd(python) alias <python-source-path> <python-target-path>
static void py_mc_alias(ExceptionSink* xsink, QoreString& arg, QorePythonProgram* pypgm) {
    // find end of qore namespace
    qore_offset_t end = arg.find(' ');
    if (end == -1 || (size_t)end == (arg.size() - 1)) {
        throw QoreStandardException("PYTHON-MODULE-ERROR", "syntax: alias <python-source-path> " \
            "<python-target-path: python target path argument; value given: '%s'", arg.c_str());
    }

    QoreString source_path(&arg, end);
    QoreString target_path(arg.c_str() + end + 1);

    pypgm->aliasDefinition(source_path, target_path);
}

// %module-cmd(python) parse <label> <source code>
static void py_mc_parse(ExceptionSink* xsink, QoreString& arg, QorePythonProgram* pypgm) {
    // find end of qore namespace
    qore_offset_t end = arg.find(' ');
    if (end == -1 || (size_t)end == (arg.size() - 1)) {
        throw QoreStandardException("PYTHON-MODULE-ERROR", "syntax: alias <python-source-path> " \
            "<python-target-path: python target path argument; value given: '%s'", arg.c_str());
    }

    QoreString source_label(&arg, end);
    QoreString source_code(arg.c_str() + end + 1);

    ValueHolder val(pypgm->eval(xsink, source_code, source_label, Py_file_input, false), xsink);
}

// %module-cmd(python) export-class <python path>
/** export a Python class to Qore
*/
static void py_mc_export_class(ExceptionSink* xsink, QoreString& arg, QorePythonProgram* pypgm) {
    pypgm->exportClass(xsink, arg);
}

QorePythonHelper::QorePythonHelper(const QorePythonProgram* pypgm) : pypgm(pypgm), old_state(pypgm->setContext()) {
}

QorePythonHelper::~QorePythonHelper() {
    pypgm->releaseContext(old_state);
}