// Copyright 2021 Ringgaard Research ApS
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

#ifndef SLING_PYAPI_PYNET_H_
#define SLING_PYAPI_PYNET_H_

#include "sling/net/http-server.h"
#include "sling/net/static-content.h"
#include "sling/pyapi/pybase.h"

namespace sling {

// Python wrapper for HTTP server.
struct PyHTTPServer : public PyBase {
  int Init(PyObject *args, PyObject *kwds);
  void Dealloc();

  // Static context.
  struct StaticContext {
    StaticContext(const string &url, const string &path) : content(url, path) {}

    // Static content handler.
    StaticContent content;

    // Next static context in linked list.
    StaticContext *next;
  };

  // Dynamic context.
  struct DynamicContext {
    // Dispatch HTTP request to Python handler.
    void Handle(HTTPRequest *request, HTTPResponse *response);

    // Parse HTTP reply.
    bool ParseReply(PyObject *ret, HTTPResponse *response);

    // Python handler.
    PyObject *handler;

    // Next dynamic context in linked list.
    DynamicContext *next;
  };

  // Start HTTP server.
  PyObject *Start();

  // Stop HTTP server.
  PyObject *Stop();

  // Add static content handler.
  PyObject *Static(PyObject *args, PyObject *kw);

  // Add dynamic content handler.
  PyObject *Dynamic(PyObject *args, PyObject *kw);

  // HTTP server.
  HTTPServer *httpd;

  // Linked list of static contexts.
  StaticContext *static_contexts;

  // Linked list of dynamic contexts.
  DynamicContext *dynamic_contexts;

  // Registration.
  static PyTypeObject type;
  static PyMethodTable methods;
  static void Define(PyObject *module);
};

}  // namespace sling

#endif  // SLING_PYAPI_PYNET_H_

