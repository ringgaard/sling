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

#include "sling/pyapi/pynet.h"

#include "sling/file/file.h"

namespace sling {

// Python type declarations.
PyTypeObject PyHTTPServer::type;
PyMethodTable PyHTTPServer::methods;

void PyHTTPServer::Define(PyObject *module) {
  InitType(&type, "sling.api.HTTPServer", sizeof(PyHTTPServer), true);
  type.tp_init = method_cast<initproc>(&PyHTTPServer::Init);
  type.tp_dealloc = method_cast<destructor>(&PyHTTPServer::Dealloc);

  methods.Add("start", &PyHTTPServer::Start);
  methods.Add("stop", &PyHTTPServer::Stop);
  methods.Add("static", &PyHTTPServer::Static);
  methods.Add("dynamic", &PyHTTPServer::Dynamic);
  type.tp_methods = methods.table();

  RegisterType(&type, module, "HTTPServer");
}

int PyHTTPServer::Init(PyObject *args, PyObject *kwds) {
  // Get arguments.
  char *addr;
  int port;
  if (!PyArg_ParseTuple(args, "si", &addr, &port)) return -1;

  // Initialize HTTP server.
  SocketServerOptions options;
  httpd = new HTTPServer(options, addr, port);
  static_contexts = nullptr;
  dynamic_contexts = nullptr;

  return 0;
}

void PyHTTPServer::Dealloc() {
  // Delete HTTP server.
  delete httpd;

  // Delete static and dynamic contexts.
  while (static_contexts != nullptr) {
    auto *s = static_contexts;
    static_contexts = static_contexts->next;
    delete s;
  }
  while (dynamic_contexts != nullptr) {
    auto *d = dynamic_contexts;
    dynamic_contexts = dynamic_contexts->next;
    Py_DECREF(d->handler);
    delete d;
  }

  Free();
}

PyObject *PyHTTPServer::Start() {
  httpd->Start();
  Py_RETURN_NONE;
}

PyObject *PyHTTPServer::Stop() {
  httpd->Shutdown();
  httpd->Wait();
  Py_RETURN_NONE;
}

PyObject *PyHTTPServer::Static(PyObject *args, PyObject *kw) {
  // Get arguments.
  char *url;
  char *path;
  if (!PyArg_ParseTuple(args, "ss", &url, &path)) return nullptr;

  // Add static context.
  StaticContext *context = new StaticContext(url, path);
  context->next = static_contexts;
  static_contexts = context;
  context->content.Register(httpd);

  Py_RETURN_NONE;
}

PyObject *PyHTTPServer::Dynamic(PyObject *args, PyObject *kw) {
  // Get arguments.
  char *url;
  PyObject *handler;
  if (!PyArg_ParseTuple(args, "sO", &url, &handler)) return nullptr;

  // Add dynamic context.
  DynamicContext *context = new DynamicContext();
  context->handler = handler;
  Py_INCREF(handler);
  context->next = dynamic_contexts;
  dynamic_contexts = context;
  httpd->Register(url, context, &DynamicContext::Handle);

  Py_RETURN_NONE;
}

void PyHTTPServer::DynamicContext::Handle(HTTPRequest *request,
                                          HTTPResponse *response) {
  // Acquire Python global interpreter lock.
  PyGILState_STATE gstate = PyGILState_Ensure();

  // Build request header argument structure.
  auto &h = request->headers();
  PyObject *headers = PyList_New(h.size());
  for (int i = 0; i < h.size(); ++i) {
    PyObject *header = PyTuple_New(2);
    PyTuple_SET_ITEM(header, 0, AllocateString(h[i].name));
    PyTuple_SET_ITEM(header, 1, AllocateString(h[i].value));
    PyList_SET_ITEM(headers, i, header);
  }

  // Call handle(method, path, query, headers, body) method on handler. Expects
  // a 3-tuple as return value (status, headers, body).
  PyObject *ret = PyObject_CallMethod(handler, "handle", "sssOy#",
    request->method(),
    request->path(),
    request->query(),
    headers,
    request->content_size() == 0 ? nullptr : request->content(),
    request->content_size());

  // Parse reply and set up response.
  if (!ParseReply(ret, response)) {
    if (PyErr_Occurred()) {
      LOG(ERROR) << "Python exception:";
      PyErr_Print();
      fflush(stderr);
    } else {
      LOG(ERROR) << "Error processing request";
    }
    response->SendError(500);
  }

  if (ret) Py_DECREF(ret);
  Py_DECREF(headers);

  // Release global interpreter lock.
  PyGILState_Release(gstate);
}

bool PyHTTPServer::DynamicContext::ParseReply(PyObject *ret,
                                              HTTPResponse *response) {
  // Check for exceptions.
  if (ret == nullptr) return false;

  // Return value should be a 4-tuple with status, headers, body, and file.
  if (!PyTuple_Check(ret)) return false;
  if (PyTuple_Size(ret) != 4) return false;

  // Get status code.
  int status = PyLong_AsLong(PyTuple_GetItem(ret, 0));
  if (status == -1) return false;
  response->set_status(status);

  // Get response headers.
  PyObject *headers = PyTuple_GetItem(ret, 1);
  if (headers != Py_None) {
    if (!PyList_Check(headers)) return false;
    for (int i = 0; i < PyList_GET_SIZE(headers); ++i) {
      PyObject *header = PyList_GET_ITEM(headers, i);
      if (!PyTuple_Check(header)) return false;
      if (PyTuple_GET_SIZE(header) != 2) return false;
      PyObject *name = PyTuple_GET_ITEM(header, 0);
      PyObject *value = PyTuple_GET_ITEM(header, 1);
      response->Add(GetString(name), GetString(value));
    }
  }

  // Get response body.
  PyObject *body = PyTuple_GetItem(ret, 2);
  if (body != Py_None) {
    if (PyBytes_Check(body)) {
      char *data;
      Py_ssize_t length;
      PyBytes_AsStringAndSize(body, &data, &length);
      response->Append(data, length);
    } else if (PyUnicode_Check(body)) {
      Py_ssize_t length;
      const char *data = PyUnicode_AsUTF8AndSize(body, &length);
      response->Append(data, length);
    } else {
      return false;
    }
  }

  // Get response file.
  PyObject *file = PyTuple_GetItem(ret, 3);
  if (file != Py_None) {
    const char *filename = GetString(file);
    if (filename == nullptr) return false;
    File *f;
    Status st = File::Open(filename, "r", &f);
    if (st.ok()) {
      // Add file to response.
      response->set_content_length(response->buffer()->available() + f->Size());
      response->SendFile(f);
    } else {
      if (st.code() == EACCES) {
        response->SendError(403, "Forbidden", nullptr);
      } else if (st.code() == ENOENT) {
        response->SendError(404, "Not Found", nullptr);
      } else {
        string error = HTMLEscape(st.message());
        response->SendError(500, "Internal Server Error", error.c_str());
      }
    }
  }

  return true;
}

}  // namespace sling

