#include <python2.7/Python.h>

namespace sling {

PyObject *helloworld(PyObject *self, PyObject *args) {
  return Py_BuildValue("s", "Hello from SLING");
}

}  // namespace sling

