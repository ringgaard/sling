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

#ifndef SLING_PYAPI_PYWEB_H_
#define SLING_PYAPI_PYWEB_H_

#include "sling/nlp/web/text-extractor.h"
#include "sling/pyapi/pybase.h"
#include "sling/web/rfc822-headers.h"
#include "sling/web/web-archive.h"

namespace sling {

// Python wrapper for WARC web archive reader.
struct PyWebArchive : public PyBase {
  int Init(PyObject *args, PyObject *kwds);
  void Dealloc();

  // Return self as iterator.
  PyObject *Self();

  // Return next record in archive as 3-tuple with web url, date, and content.
  PyObject *Next();

  // WARC file.
  WARCFile *warc;

  // Registration.
  static PyTypeObject type;
  static void Define(PyObject *module);
};

// Pyhton wrapper for website analysis for text extraction.
struct PyWebsiteAnalysis : public PyBase {
  int Init(PyObject *args, PyObject *kwds);
  void Dealloc();

  // Analyze web page and update analysis.
  PyObject *Analyze(PyObject *pycontent);

  // Extract text and meta data from HTML page.
  PyObject *Extract(PyObject *pycontent);

  // Return analysis fingerprints.
  PyObject *Fingerprints();

  // Web site analysis.
  nlp::WebsiteAnalysis *analysis;

  // Registration.
  static PyTypeObject type;
  static PyMethodTable methods;
  static void Define(PyObject *module);
};

// Python wrapper for web page.
struct PyWebPage : public PyBase {
  int Init(PyWebsiteAnalysis *analysis, PyObject *pycontent);
  void Dealloc();

  // Return extracted text.
  PyObject *GetExtractedText();

  // Return dictionary with HTTP headers.
  PyObject *GetHeaders();

  // Return dictionary with web page meta data.
  PyObject *GetMetaData();

  // Return array with LD-JSON blocks.
  PyObject *GetLDJSON();

  // Web site analysis.
  PyWebsiteAnalysis *pyanalysis;

  // HTTP headers.
  RFC822Headers *headers;

  // Web page extractor.
  nlp::WebPageTextExtractor *extractor;

  // Registration.
  static PyTypeObject type;
  static PyMethodTable methods;
  static void Define(PyObject *module);
};

}  // namespace sling

#endif  // SLING_PYAPI_PYWEB_H_
