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
#include "sling/stream/memory.h"
#include "sling/web/web-archive.h"

namespace sling {

// Python type declarations.
PyTypeObject PyWebArchive::type;
PyTypeObject PyWebsiteAnalysis::type;
PyMethodTable PyWebsiteAnalysis::methods;

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

  // Create (url, date, content) tuple.
  PyObject *uri = Py_None;
  PyObject *date = Py_None;
  PyObject *content = Py_None;
  if (!warc->uri().empty()) {
    uri = PyBytes_FromStringAndSize(warc->uri().data(), warc->uri().size());
  }
  if (!warc->date().empty()) {
    date = AllocateString(warc->date());
  }
  int length = warc->content_length();
  if (warc->content_length() > 0) {
    // Create byte array.
    content = PyBytes_FromStringAndSize(nullptr, length);

    // Get buffer for byte array.
    Py_buffer buffer;
    if (PyObject_GetBuffer(content, &buffer, PyBUF_SIMPLE) < 0) return nullptr;

    // Copy content into buffer.
    Input input(warc->content());
    CHECK(input.Read(static_cast<char *>(buffer.buf), length));
    PyBuffer_Release(&buffer);
  }
  PyObject *tuple = PyTuple_Pack(3, uri, date, content);
  if (uri != Py_None) Py_DECREF(uri);
  if (date != Py_None) Py_DECREF(date);
  if (content != Py_None) Py_DECREF(content);

  return tuple;
}

void PyWebsiteAnalysis::Define(PyObject *module) {
  InitType(&type, "sling.WebsiteAnalysis", sizeof(PyWebsiteAnalysis), true);
  type.tp_init = method_cast<initproc>(&PyWebsiteAnalysis::Init);
  type.tp_dealloc = method_cast<destructor>(&PyWebsiteAnalysis::Dealloc);

  methods.AddO("analyze", &PyWebsiteAnalysis::Analyze);
  methods.AddO("extract", &PyWebsiteAnalysis::Extract);
  methods.Add("fingerprints", &PyWebsiteAnalysis::Fingerprints);
  type.tp_methods = methods.table();

  RegisterType(&type, module, "WebsiteAnalysis");
}

int PyWebsiteAnalysis::Init(PyObject *args, PyObject *kwds) {
  // Initialize web site analysis.
  analysis = new nlp::WebsiteAnalysis();

  return 0;
}

void PyWebsiteAnalysis::Dealloc() {
  delete analysis;
  Free();
}

PyObject *PyWebsiteAnalysis::Analyze(PyObject *html) {
  // Get HTML page content.
  Text content = GetText(html);
  if (content.data() == nullptr) return nullptr;

  // Set up input stream for parsing.
  ArrayInputStream stream(content.data(), content.size());
  Input input(&stream);

  // Analyze page.
  nlp::WebPageAnalyzer analyzer(analysis);
  analyzer.Parse(&input);
  Py_RETURN_NONE;
}

PyObject *PyWebsiteAnalysis::Extract(PyObject *html) {
  // Get HTML page content.
  Text content = GetText(html);
  if (content.data() == nullptr) return nullptr;

  // Set up input stream for parsing.
  ArrayInputStream stream(content.data(), content.size());
  Input input(&stream);

  // Extract HTTP headers.
  RFC822Headers headers;
  headers.Parse(&input);

  // Extract text from HTML page.
  nlp::WebPageTextExtractor extractor(analysis);
  extractor.Parse(&input);
  return AllocateString(extractor.text());
}

PyObject *PyWebsiteAnalysis::Fingerprints() {
  // Get fingerprints from analysis.
  std::vector<uint64> fps;
  analysis->GetFingerprints(&fps);

  // Return fingerprints as raw unsigned 64-bit integers.
  const char *data = reinterpret_cast<const char *>(fps.data());
  return PyBytes_FromStringAndSize(data, fps.size() * sizeof(uint64));
}

}  // namespace sling

