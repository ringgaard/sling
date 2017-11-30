#ifndef SLING_API_FRAMES_H_
#define SLING_API_FRAMES_H_

#include <python2.7/Python.h>

#include "sling/frame/store.h"

namespace sling {

class PyClass : public PyVarObject {
};

// Python wrapper for frame store.
struct PyStore : public PyClass {
  Store *store = nullptr;

  // Registration.
  static PyTypeObject type;
  static void Define(PyObject *module);
};

}  // namespace sling

#endif  // SLING_API_FRAMES_H_

