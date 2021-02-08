// Copyright 2020 Ringgaard Research ApS
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

#include "sling/pyapi/pystring.h"

namespace sling {

// Python type declarations.
PyTypeObject PyString::type;
PyMethodTable PyString::methods;

void PyString::Define(PyObject *module) {
  InitType(&type, "sling.String", sizeof(PyString), false);
  type.tp_dealloc = method_cast<destructor>(&PyString::Dealloc);
  type.tp_str = method_cast<reprfunc>(&PyString::Str);
  type.tp_hash = method_cast<hashfunc>(&PyString::Hash);

  methods.Add("text", &PyString::Str);
  methods.Add("qualifier", &PyString::Qualifier);
  type.tp_methods = methods.table();

  RegisterType(&type, module, "String");
}

void PyString::Init(PyStore *pystore, Handle handle) {
  // Add reference to store to keep it alive.
  if (handle.IsGlobalRef() && pystore->pyglobals != nullptr) {
    this->pystore = pystore->pyglobals;
  } else {
    this->pystore = pystore;
  }
  Py_INCREF(this->pystore);

  // Add string as root object for store to keep it alive in the store.
  InitRoot(this->pystore->store, handle);
}

void PyString::Dealloc() {
  // Unlock tracking of handle in store.
  Unlink();

  // Release reference to store.
  Py_DECREF(pystore);

  // Free object.
  Free();
}

long PyString::Hash() {
  return handle().bits;
}

PyObject *PyString::Str() {
  return AllocateString(string()->str());
}

PyObject *PyString::Qualifier() {
  return pystore->PyValue(string()->qualifier());
}

}  // namespace sling

