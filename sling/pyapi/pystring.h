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

#ifndef SLING_PYAPI_PYSTRING_H_
#define SLING_PYAPI_PYSTRING_H_

#include "sling/pyapi/pybase.h"
#include "sling/pyapi/pystore.h"
#include "sling/frame/store.h"

namespace sling {

// Python wrapper for qualified string.
struct PyString : public PyBase, public Root {
  // Initialize string wrapper.
  void Init(PyStore *pystore, Handle handle);

  // Deallocate string wrapper.
  void Dealloc();

  // Return hash value for string.
  long Hash();

  // Return string as Python string.
  PyObject *Str();

  // Return string qualifier.
  PyObject *Qualifier();

  // Return handle for string.
  Handle handle() const { return handle_; }

  // Dereference string reference.
  StringDatum *string() { return pystore->store->Deref(handle())->AsString(); }

  // Store for array.
  PyStore *pystore;

  // Registration.
  static PyTypeObject type;
  static PyMethodTable methods;
  static void Define(PyObject *module);
};

}  // namespace sling

#endif  // SLING_PYAPI_PYSTRING_H_

