#ifndef SLING_API_FRAMES_H_
#define SLING_API_FRAMES_H_

#include <python2.7/Python.h>

#include "sling/frame/store.h"

namespace sling {

class PyFrame;

class PyClass : public PyVarObject {
 protected:
  static void InitType(PyTypeObject *type,
                       const char *name, size_t size);
  static void RegisterType(PyObject *module,
                           PyTypeObject *type,
                           const char *name);
};

// Python wrapper for frame store.
struct PyStore : public PyClass {
  // Initialize new store.
  int Init(PyObject *args, PyObject *kwds);

  // Deallocate store.
  void Dealloc();

  // Freeze store.
  PyObject *Freeze();

  // Load frames from file.
  PyObject *Load(PyObject *args);

  // Save frames to file.
  PyObject *Save(PyObject *args);

  // Read frames from string.
  PyObject *Read(PyObject *args);

  // Return the number of objects in the symbol table.
  Py_ssize_t Size();

  // Look up object in symbol table.
  PyObject *Lookup(PyObject *key);

  // Create new Python object for handle value.
  PyObject *ToPython(Handle handle);

  // Underlying frame store.
  Store *store = nullptr;

  // Registration.
  static PyTypeObject type;
  static PyMappingMethods mapping;
  static PyMethodDef methods[];
  static void Define(PyObject *module);
};

// Python wrapper for frame.
struct PyFrame : public PyClass, public Root {
  // Initialize frame wrapper.
  void Init(PyStore *store, Handle handle);

  // Deallocate frame wrapper.
  void Dealloc();

  // Store for frame.
  PyStore *store = nullptr;

  // Registration.
  static PyTypeObject type;
  static PyMethodDef methods[];
  static void Define(PyObject *module);
};

}  // namespace sling

#endif  // SLING_API_FRAMES_H_

