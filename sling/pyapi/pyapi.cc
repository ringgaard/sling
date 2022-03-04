// Copyright 2017 Google Inc.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "sling/base/init.h"

#include "sling/pyapi/pyarray.h"
#include "sling/pyapi/pybase.h"
#include "sling/pyapi/pydatabase.h"
#include "sling/pyapi/pydate.h"
#include "sling/pyapi/pyframe.h"
#include "sling/pyapi/pymyelin.h"
#include "sling/pyapi/pynet.h"
#include "sling/pyapi/pyparser.h"
#include "sling/pyapi/pyphrase.h"
#include "sling/pyapi/pyrecordio.h"
#include "sling/pyapi/pystore.h"
#include "sling/pyapi/pystring.h"
#include "sling/pyapi/pywiki.h"
#include "sling/pyapi/pymisc.h"
#include "sling/pyapi/pytask.h"
#include "sling/pyapi/pyweb.h"

namespace sling {

static PyMethodDef py_funcs[] = {
  {"get_flags", (PyCFunction) PyGetFlags, METH_NOARGS, ""},
  {"set_flag", (PyCFunction) PySetFlag, METH_VARARGS, ""},
  {"log_message", (PyCFunction) PyLogMessage, METH_VARARGS, ""},
  {"create_pid_file", (PyCFunction) PyCreatePIDFile, METH_NOARGS, ""},
  {"register_task", (PyCFunction) PyRegisterTask, METH_VARARGS, ""},
  {"start_task_monitor", (PyCFunction) PyStartTaskMonitor, METH_VARARGS, ""},
  {"get_job_statistics", (PyCFunction) PyGetJobStatistics, METH_NOARGS, ""},
  {"finalize_dashboard", (PyCFunction) PyFinalizeDashboard, METH_NOARGS, ""},
  {"tolex", (PyCFunction) PyToLex, METH_VARARGS, ""},
  {"evaluate_frames", (PyCFunction) PyEvaluateFrames, METH_VARARGS, ""},
  {"cpus", (PyCFunction) PyCPUs, METH_NOARGS, ""},
  {"cores", (PyCFunction) PyCores, METH_NOARGS, ""},
  {nullptr, nullptr, 0, nullptr}
};

static struct PyModuleDef py_module = {
  PyModuleDef_HEAD_INIT,
  "pysling",
  nullptr,
  0,
  py_funcs,
  nullptr,
  nullptr,
  nullptr,
  nullptr
};

static PyObject *RegisterPythonModule() {
  PyObject *module = PyModule_Create(&py_module);

  PyStore::Define(module);
  PyString::Define(module);
  PySymbols::Define(module);
  PyFrame::Define(module);
  PySlots::Define(module);
  PyArray::Define(module);
  PyItems::Define(module);

  PyTokenizer::Define(module);
  PyParser::Define(module);
  PyAnalyzer::Define(module);

  PyPhraseMatch::Define(module);
  PyPhraseTable::Define(module);

  PyRecordReader::Define(module);
  PyRecordWriter::Define(module);
  PyRecordDatabase::Define(module);

  PyDatabase::Define(module);
  PyCursor::Define(module);

  PyCalendar::Define(module);
  PyDate::Define(module);

  PyWikiConverter::Define(module);
  PyFactExtractor::Define(module);
  PyTaxonomy::Define(module);
  PyPlausibility::Define(module);

  PyCompiler::Define(module);
  PyNetwork::Define(module);
  PyCell::Define(module);
  PyInstance::Define(module);
  PyChannel::Define(module);
  PyTensor::Define(module);

  PyJob::Define(module);
  PyResource::Define(module);
  PyTask::Define(module);
  PyWebArchive::Define(module);
  PyWebsiteAnalysis::Define(module);
  PyHTTPServer::Define(module);

  return module;
}

}  // namespace sling

PyMODINIT_FUNC PyInit_pysling() {
  sling::InitSharedLibrary();
  return sling::RegisterPythonModule();
}

