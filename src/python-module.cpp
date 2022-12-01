/* indent-tabs-mode: nil -*- */
/*
    python Qore module

    Copyright (C) 2020 - 2021 Qore Technologies, s.r.o.

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

static QoreStringNode* python_module_init_intern(bool repeat);

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

    mod_info.info = new QoreHashNode(autoTypeInfo);
    mod_info.info->setKeyValue("python_version", new QoreStringNodeMaker(PY_VERSION), nullptr);
    mod_info.info->setKeyValue("python_major", PY_MAJOR_VERSION, nullptr);
    mod_info.info->setKeyValue("python_minor", PY_MINOR_VERSION, nullptr);
    mod_info.info->setKeyValue("python_micro", PY_MICRO_VERSION, nullptr);
}

QoreNamespace* PNS = nullptr;
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
static void py_mc_export_func(ExceptionSink* xsink, QoreString& arg, QorePythonProgram* pypgm);
static void py_mc_add_module_path(ExceptionSink* xsink, QoreString& arg, QorePythonProgram* pypgm);
//static void py_mc_reset_python(ExceptionSink* xsink, QoreString& arg, QorePythonProgram* pypgm);

struct qore_python_cmd_info_t {
    qore_python_module_cmd_t cmd;
    bool requires_arg = true;

    DLLLOCAL qore_python_cmd_info_t(qore_python_module_cmd_t cmd, bool requires_arg)
        : cmd(cmd), requires_arg(requires_arg) {
    }
};

// module cmds
typedef std::map<std::string, qore_python_cmd_info_t> mcmap_t;
static mcmap_t mcmap = {
    {"import", qore_python_cmd_info_t(py_mc_import, true)},
    {"import-ns", qore_python_cmd_info_t(py_mc_import_ns, true)},
    {"alias", qore_python_cmd_info_t(py_mc_alias, true)},
    {"parse", qore_python_cmd_info_t(py_mc_parse, true)},
    {"export-class", qore_python_cmd_info_t(py_mc_export_class, true)},
    {"export-func", qore_python_cmd_info_t(py_mc_export_func, true)},
    {"add-module-path", qore_python_cmd_info_t(py_mc_add_module_path, true)},
#if 0
    {"reset-python", qore_python_cmd_info_t(py_mc_reset_python, false)},
#endif
};

static bool python_needs_shutdown = false;
static bool python_initialized = false;
bool python_shutdown = false;

int python_u_tld_key = -1;
int python_qobj_key = -1;

static sig_vec_t sig_vec = {
#ifndef _Q_WINDOWS
    SIGSEGV, SIGBUS
#endif
};

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

static void python_module_shutdown() {
    if (python_initialized) {
        PyThreadState_Swap(nullptr);
        PyEval_AcquireThread(mainThreadState);
        _qore_PyGILState_SetThisThreadState(mainThreadState);
    }
    python_shutdown = true;
    if (python_needs_shutdown) {
        int rc = Py_FinalizeEx();
        if (rc) {
            printd(0, "Unkown error shutting down Python: rc: %d\n", rc);
        }
        python_needs_shutdown = false;
    }
}

#if 0
// does not work with modules like tensorflow that do not unload cleanly
int q_reset_python(ExceptionSink* xsink) {
    if (!python_needs_shutdown) {
        xsink->raiseException("PYTHON-RESET-ERROR", "The module was loaded into an existing Python process and " \
            "therefore cannot be reset externally");
        return -1;
    }

    unsigned cnt = QorePythonProgram::getProgramCount();
    if (cnt) {
        if (cnt <= 2) {
            QoreProgram* pgm = getProgram();
            if (pgm) {
                QorePythonProgramData* pypgm = static_cast<QorePythonProgramData*>(pgm->removeExternalData(QORE_PYTHON_MODULE_NAME));
                if (pypgm) {
                    pypgm->destructor(xsink);
                    pypgm->weakDeref();
                    if (*xsink) {
                        return -1;
                    }
                    --cnt;
                }
            }
        }

        if (cnt == 1 && qore_python_pgm) {
            qore_python_pgm->destructor(xsink);
            qore_python_pgm->weakDeref();
            qore_python_pgm = nullptr;
            --cnt;
        }

        if (cnt) {
            xsink->raiseException("PYTHON-RESET-ERROR", "Cannot reset the Python library with %d Python program%s " \
                "still valid", cnt, cnt == 1 ? "" : "s");
            return -1;
        }
    }

    python_module_shutdown();

    SimpleRefHolder<QoreStringNode> err(python_module_init_intern(true));
    if (err) {
        xsink->raiseException("PYTHON-RESET-ERROR", err.release());
        return -1;
    }

    return 0;
}
#endif

static QoreStringNode* python_module_init() {
    return python_module_init_intern(false);
}

static QoreStringNode* python_module_init_intern(bool repeat) {
    if (!PNS) {
        PNS = new QoreNamespace("Python");
        PNS->addSystemClass(initPythonProgramClass(*PNS));
        QC_PYTHONBASEOBJECT = new QorePythonClass("__qore_base__", "::Python::__qore_base__");
        CID_PYTHONBASEOBJECT = QC_PYTHONBASEOBJECT->getID();

        PNS->addSystemClass(QC_PYTHONBASEOBJECT->copy());
    }

    // initialize python library; do not register signal handlers
    if (!Py_IsInitialized()) {
        if (PyImport_AppendInittab("qoreloader", PyInit_qoreloader) == -1) {
            throw QoreStandardException("PYTHON-MODULE-ERROR", "cannot append the qoreloader module to Python");
        }

        Py_InitializeEx(0);
#ifdef QORE_ALLOW_PYTHON_SHUTDOWN
        // issue# 4290: if we actively shut down Python on exit, then exit handlers in modules
        // (such as the h5py module in version 3.3.0) will cause a crash when the process exits,
        // as it requires the Python library to be still in place and initialized
        python_needs_shutdown = true;
#endif
        python_initialized = true;
        //printd(5, "python_module_init() Python initialized\n");
    }

    if (!repeat) {
#ifndef _Q_WINDOWS
        sig_vec_t new_sig_vec;
        for (int sig : sig_vec) {
            QoreStringNode *err = qore_reassign_signal(sig, QORE_PYTHON_MODULE_NAME);
            if (err) {
                // ignore errors; already assigned to another module
                err->deref();
            }
            new_sig_vec.push_back(sig);
        }
        if (!new_sig_vec.empty()) {
            sigset_t mask;
            // setup signal mask
            sigemptyset(&mask);
            for (auto& sig : new_sig_vec) {
                //printd(LogLevel, "python_module_init() unblocking signal %d\n", sig);
                sigaddset(&mask, sig);
            }
            // unblock threads
            pthread_sigmask(SIG_UNBLOCK, &mask, 0);
        }
#endif

        python_u_tld_key = q_get_unique_thread_local_data_key();
        python_qobj_key = q_get_unique_thread_local_data_key();
    }

    // ensure that runtime version matches compiled version
    check_python_version();

    if (init_global_qore_python_pgm() || QorePythonProgram::staticInit()) {
        throw QoreStandardException("PYTHON-MODULE-ERROR", "failed to initialize \"python\" module");
    }

    mainThreadState = PyThreadState_Get();
    if (python_initialized) {
        // release the current thread state after initialization
        PyEval_ReleaseThread(mainThreadState);
        assert(!_qore_PyRuntimeGILState_GetThreadState());
        _qore_PyGILState_SetThisThreadState(nullptr);
        assert(!PyGILState_GetThisThreadState());
        assert(!QorePythonProgram::haveGil());
    }

    if (!repeat) {
        tclist.push(QorePythonProgram::pythonThreadCleanup, nullptr);
    }

    return nullptr;
}

static void python_module_ns_init(QoreNamespace* rns, QoreNamespace* qns) {
    QoreProgram* pgm = getProgram();
    assert(pgm->getRootNS() == rns);
    if (!pgm->getExternalData(QORE_PYTHON_MODULE_NAME)) {
        QoreNamespace* pyns = PNS->copy();
        rns->addNamespace(pyns);
        // issue #4153: in case we only have the calling context here
        ExceptionSink xsink;
        QoreExternalProgramContextHelper pch(&xsink, pgm);
        if (!xsink) {
            pgm->setExternalData(QORE_PYTHON_MODULE_NAME, new QorePythonProgram(pgm, pyns));
        }
    }

    assert(!python_initialized || !PyGILState_Check());
    assert(!python_initialized || !QorePythonProgram::haveGil());
}

static void python_module_delete() {
    if (qore_python_pgm) {
        qore_python_pgm->doDeref();
        qore_python_pgm = nullptr;
    }
    if (PNS) {
        delete PNS;
        PNS = nullptr;
    }
    python_module_shutdown();
}

static void python_module_parse_cmd(const QoreString& cmd, ExceptionSink* xsink) {
    //printd(5, "python_module_parse_cmd() cmd: '%s'\n", cmd.c_str());

    const char* p = strchr(cmd.c_str(), ' ');
    QoreString str;
    QoreString arg;
    if (p) {
        QoreString nstr(&cmd, p - cmd.c_str());
        str = nstr;
        arg = cmd;
        arg.replace(0, p - cmd.c_str() + 1, (const char*)nullptr);
        arg.trim();
    } else {
        str = cmd;
        str.trim();
    }

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

    if (i->second.requires_arg) {
        if (arg.empty()) {
            xsink->raiseException("PYTHON-PARSE-COMMAND-ERROR", "missing argument / command name in parse command: '%s'", cmd.c_str());
            return;
        }
    } else {
        if (!arg.empty()) {
            xsink->raiseException("PYTHON-PARSE-COMMAND-ERROR", "extra argument / command name in parse command: '%s'", cmd.c_str());
            return;
        }
    }

    QoreProgram* pgm = getProgram();
    QorePythonProgram* pypgm = static_cast<QorePythonProgram*>(pgm->getExternalData(QORE_PYTHON_MODULE_NAME));
    //printd(5, "parse-cmd '%s' pypgm: %p pythonns: %p\n", arg.c_str(), pypgm, pypgm->getPythonNamespace());
    if (!pypgm) {
        QoreNamespace* pyns = PNS->copy();
        pgm->getRootNS()->addNamespace(pyns);
        pypgm = new QorePythonProgram(pgm, pyns);
        pgm->setExternalData(QORE_PYTHON_MODULE_NAME, pypgm);
        pgm->addFeature(QORE_PYTHON_MODULE_NAME);
    }

    i->second.cmd(xsink, arg, pypgm);
}

// %module-cmd(python) import
static void py_mc_import(ExceptionSink* xsink, QoreString& arg, QorePythonProgram* pypgm) {
    // process import statement
    //printd(5, "py_mc_import() pypgm: %p arg: %s\n", pypgm, arg.c_str());

    QorePythonHelper qph(pypgm);

    // see if there is a dot (.) in the name
    qore_offset_t i = arg.find('.');
    if (i < 0 || i == static_cast<qore_offset_t>(arg.size() - 1)) {
        pypgm->import(xsink, arg.c_str());
        return;
    }

    const char* symbol = arg.c_str() + i + 1;
    arg.replaceChar(i, '\0');

    arg.terminate(i);
    if (!strcmp(symbol, "*")) {
        pypgm->import(xsink, arg.c_str());
        return;
    }

    pypgm->import(xsink, arg.c_str(), symbol);
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

// %module-cmd(python) export-func <python path>
/** export a Python function to Qore
*/
static void py_mc_export_func(ExceptionSink* xsink, QoreString& arg, QorePythonProgram* pypgm) {
    pypgm->exportFunction(xsink, arg);
}

// %module-cmd(python) add-module-path <fs path>
/** add a path to the module path
*/
static void py_mc_add_module_path(ExceptionSink* xsink, QoreString& arg, QorePythonProgram* pypgm) {
    pypgm->addModulePath(xsink, arg);
}

#if 0
// %module-cmd(python) reset-python
static void py_mc_reset_python(ExceptionSink* xsink, QoreString& arg, QorePythonProgram* pypgm) {
    q_reset_python(xsink);
}
#endif

// exported function
extern "C" int python_module_import(ExceptionSink* xsink, QoreProgram* pgm, const char* module, const char* symbol) {
    QorePythonProgram* pypgm = static_cast<QorePythonProgram*>(pgm->getExternalData(QORE_PYTHON_MODULE_NAME));
    if (!pypgm) {
        QoreNamespace* pyns = PNS->copy();
        pgm->getRootNS()->addNamespace(pyns);
        pypgm = new QorePythonProgram(pgm, pyns);
        pgm->setExternalData(QORE_PYTHON_MODULE_NAME, pypgm);
        pgm->addFeature(QORE_PYTHON_MODULE_NAME);
    }
    // the following call adds the class to the current program as well
    QorePythonHelper qph(pypgm);
    return pypgm->import(xsink, module, symbol);
}

QorePythonHelper::QorePythonHelper(const QorePythonProgram* pypgm)
    : old_pgm(q_swap_thread_local_data(python_u_tld_key, (void*)pypgm)), old_state(pypgm->setContext()), new_pypgm(pypgm) {
    //printd(5, "QorePythonHelper::QorePythonHelper() new: %p old: %p\n", pypgm, old_pgm);
}

QorePythonHelper::~QorePythonHelper() {
    new_pypgm->releaseContext(old_state);
    q_swap_thread_local_data(python_u_tld_key, (void*)old_pgm);
    //printd(5, "QorePythonHelper::~QorePythonHelper() restored old: %p\n", old_pgm);
}

bool _qore_has_gil(PyThreadState* t_state) {
    return (_qore_PyCeval_GetGilLockedStatus() && _qore_PyCeval_GetThreadState() == t_state);
}

static bool _qore_has_gil(PyThreadState* state0, PyThreadState* state1) {
    if (!_qore_PyCeval_GetGilLockedStatus()) {
        return false;
    }
    PyThreadState* gs = _qore_PyCeval_GetThreadState();
    return gs == state0 || gs == state1;
}

QorePythonGilHelper::QorePythonGilHelper(PyThreadState* new_thread_state)
    : new_thread_state(new_thread_state), state(_qore_PyRuntimeGILState_GetThreadState()),
        t_state(PyGILState_GetThisThreadState()),
        release_gil(!_qore_has_gil(t_state, new_thread_state)) {
    //printd(5, "QorePythonGilHelper::QorePythonGilHelper() %llx acquire: %d state: %llx t_state: %llx\n",
    //    new_thread_state, release_gil, state, t_state);
    assert(new_thread_state);
    if (release_gil) {
        PyEval_AcquireThread(new_thread_state);
        assert(PyThreadState_Get() == new_thread_state);
    } else {
        assert(t_state == _qore_PyCeval_GetThreadState());
    }
    // NOTE: even if the current thread state is equal to the new one, we still need to set all thread states in all
    // locations

    ++new_thread_state->gilstate_counter;
    PyThreadState_Swap(new_thread_state);

    // set this thread state
    _qore_PyGILState_SetThisThreadState(new_thread_state);
    assert(PyGILState_GetThisThreadState() == new_thread_state);
    assert(PyGILState_Check());
}

QorePythonGilHelper::~QorePythonGilHelper() {
    assert(_qore_has_gil());

    --new_thread_state->gilstate_counter;

    if (release_gil) {
        //printd(5, "QorePythonGilHelper::~QorePythonGilHelper() releasing %llx state: %llx t_state: %llx\n",
        //    new_thread_state, state, t_state);
        PyThreadState_Swap(new_thread_state);
        _qore_PyCeval_SwapThreadState(new_thread_state);
        _qore_PyGILState_SetThisThreadState(new_thread_state);
        // release the GIL
        PyEval_ReleaseThread(new_thread_state);
    } else {
        //printd(5, "QorePythonGilHelper::~QorePythonGilHelper() swapping %llx state: %llx t_state: %llx\n",
        //    new_thread_state, state, t_state);
        PyThreadState_Swap(state);
        _qore_PyCeval_SwapThreadState(t_state);
    }

    // restore the old TLD state
    _qore_PyGILState_SetThisThreadState(t_state);
}

void QorePythonGilHelper::set(PyThreadState* other_state) {
    // as this is called after creating a new interpreter, we cannot assert that we hold the GIL here
    assert(_qore_PyCeval_GetGilLockedStatus() && _qore_PyCeval_GetThreadState());

    PyThreadState_Swap(other_state);
    _qore_PyCeval_SwapThreadState(other_state);
    _qore_PyGILState_SetThisThreadState(other_state);
}
