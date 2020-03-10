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

#include "sling/pyapi/pyweb.h"

#include "sling/stream/file-input.h"
#include "sling/stream/input.h"
#include "sling/web/web-archive.h"

namespace sling {

// Python type declarations.
PyTypeObject PyWebArchive::type;

void PyWebArchive::Define(PyObject *module) {
  InitType(&type, "sling.WebArchive", sizeof(PyWebArchive), true);
  type.tp_init = method_cast<initproc>(&PyWebArchive::Init);
  type.tp_dealloc = method_cast<destructor>(&PyWebArchive::Dealloc);
  type.tp_iter = method_cast<getiterfunc>(&PyWebArchive::Self);
  type.tp_iternext = method_cast<iternextfunc>(&PyWebArchive::Next);

  RegisterType(&type, module, "WebArchive");
}

int PyWebArchive::Init(PyObject *args, PyObject *kwds) {
  // Get arguments.
  char *filename;
  if (!PyArg_ParseTuple(args, "s", &filename)) {
    return -1;
  }

  // Open web archive.
  warc = new WARCFile(filename);

  return 0;
}

void PyWebArchive::Dealloc() {
  delete warc;
  Free();
}

PyObject *PyWebArchive::Self() {
  Py_INCREF(this);
  return AsObject();
}

PyObject *PyWebArchive::Next() {
  // Check if there are more records.
  if (!warc->Next()) {
    PyErr_SetNone(PyExc_StopIteration);
    return nullptr;
  }

  // Create url and content tuple.
  PyObject *k = Py_None;
  PyObject *v = Py_None;
  if (!warc->uri().empty()) {
    k = PyBytes_FromStringAndSize(warc->uri().data(), warc->uri().size());
  }
  int length = warc->content_length();
  if (warc->content_length() > 0) {
    // Create byte array.
    v = PyBytes_FromStringAndSize(nullptr, length);

    // Get buffer for byte array.
    Py_buffer buffer;
    if (PyObject_GetBuffer(v, &buffer, PyBUF_SIMPLE) < 0) return nullptr;

    // Copy content into buffer.
    Input input(warc->content());
    CHECK(input.Read(static_cast<char *>(buffer.buf), length));
    PyBuffer_Release(&buffer);
  }
  PyObject *pair = PyTuple_Pack(2, k, v);
  if (k != Py_None) Py_DECREF(k);
  if (v != Py_None) Py_DECREF(v);

  return pair;
}

}  // namespace sling

