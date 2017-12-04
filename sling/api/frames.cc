#include "sling/api/frames.h"

#include <python2.7/Python.h>

#include "sling/base/logging.h"
#include "sling/file/file.h"
#include "sling/frame/serialization.h"
#include "sling/frame/store.h"
#include "sling/stream/file.h"
#include "sling/stream/unix-file.h"

namespace sling {

PyTypeObject PyStore::type;
PyMappingMethods PyStore::mapping;
PyTypeObject PyFrame::type;

void PyClass::InitType(PyTypeObject *type,
                       const char *name, size_t size) {
  type->tp_name = name;
  type->tp_basicsize = size;
  type->tp_new = PyType_GenericNew;
  type->tp_flags = Py_TPFLAGS_DEFAULT | Py_TPFLAGS_BASETYPE;
}

void PyClass::RegisterType(PyObject *module,
                           PyTypeObject *type,
                           const char *name) {
  PyType_Ready(type);
  Py_INCREF(type);
  PyModule_AddObject(module, name, reinterpret_cast<PyObject *>(type));
}

PyMethodDef PyStore::methods[] = {
  {"freeze", (PyCFunction) &PyStore::Freeze, METH_NOARGS, ""},
  {"load", (PyCFunction) &PyStore::Load, METH_VARARGS, ""},
  {"save", (PyCFunction) &PyStore::Save, METH_VARARGS, ""},
  {"read", (PyCFunction) &PyStore::Read, METH_VARARGS, ""},
  {nullptr}
};

void PyStore::Define(PyObject *module) {
  PyTypeObject *type = &PyStore::type;
  InitType(type, "sling.Store", sizeof(PyStore));

  type->tp_init = reinterpret_cast<initproc>(&PyStore::Init);
  type->tp_dealloc = reinterpret_cast<destructor>(&PyStore::Dealloc);
  type->tp_methods = methods;
  type->tp_as_mapping = &mapping;

  mapping.mp_length = &PyStore::Size;
  mapping.mp_subscript = &PyStore::Lookup;

  RegisterType(module, type, "Store");
}

int PyStore::Init(PyObject *args, PyObject *kwds) {
  // Get optional globals argument.
  PyStore *globals = nullptr;
  if (!PyArg_ParseTuple(args, "|O", &globals)) return -1;

  // Create new store.
  if (globals != nullptr) {
    LOG(INFO) << "Create local store";
    // Check that argument is a store.
    if (!PyObject_TypeCheck(globals, &type)) return -1;

    // Check that global has been frozen.
    if (!globals->store->frozen()) {
      PyErr_SetString(PyExc_ValueError, "Global store is not frozen");
      return -1;
    }

    // Create local store.
    store = new Store(globals->store);
  } else {
    LOG(INFO) << "Create global store";
    store = new Store();
  }

  return 0;
}

void PyStore::Dealloc() {
  LOG(INFO) << "Dealloc store";
  delete store;
}

PyObject *PyStore::Freeze() {
  store->Freeze();
  Py_RETURN_NONE;
}

PyObject *PyStore::Load(PyObject *args) {
  LOG(INFO) << "Load called";

  // Get file or filename argument.
  PyObject *file = nullptr;
  if (!PyArg_ParseTuple(args, "O", &file)) return nullptr;

  // Check that global is not frozen.
  if (store->frozen()) {
    PyErr_SetString(PyExc_ValueError, "Store is frozen");
    return nullptr;
  }

  // Read frames from file.
  if (PyFile_Check(file)) {
    // Load store from file object.
    StdFileInputStream stream(PyFile_AsFile(file), false);
    InputParser parser(store, &stream);
    parser.ReadAll();
    if (parser.error()) {
      PyErr_SetString(PyExc_IOError, parser.error_message().c_str());
      return nullptr;
    }
  } else if (PyString_Check(file)) {
    // Load store store from file. First, open input file.
    File *f;
    Status st = File::Open(PyString_AsString(file), "r", &f);
    if (!st.ok()) {
      PyErr_SetString(PyExc_IOError, st.message());
      return nullptr;
    }

    // Load frames from file.
    FileInputStream stream(f);
    InputParser parser(store, &stream);
    parser.ReadAll();
    if (parser.error()) {
      PyErr_SetString(PyExc_IOError, parser.error_message().c_str());
      return nullptr;
    }
  } else {
    PyErr_SetString(PyExc_ValueError, "File or string argument expected");
    return nullptr;
  }

  Py_RETURN_NONE;
}

PyObject *PyStore::Save(PyObject *args) {
  LOG(INFO) << "Save called";
  Py_RETURN_NONE;
}

PyObject *PyStore::Read(PyObject *args) {
  LOG(INFO) << "Read called";
  Py_RETURN_NONE;
}

Py_ssize_t PyStore::Size() {
  return store->NumSymbols();
}

PyObject *PyStore::Lookup(PyObject *key) {
  Py_RETURN_NONE;
}

PyObject *PyStore::ToPython(Handle handle) {
#if 0
  // Allocate new frame wrapper.
  PyFrame *frame = PyObject_New(PyFrame, &PyFrame::type);

  return frame;
#endif
  return nullptr;
}

PyMethodDef PyFrame::methods[] = {
  {nullptr}
};

void PyFrame::Define(PyObject *module) {
  PyTypeObject *type = &PyFrame::type;
  InitType(type, "sling.Frame", sizeof(PyFrame));
  type->tp_dealloc = reinterpret_cast<destructor>(&PyFrame::Dealloc);
  type->tp_methods = methods;
}

void PyFrame::Init(PyStore *store, Handle handle) {
  // Store reference to frame in store.
  this->store = store;
  InitRoot(store->store, handle);

  // Add reference to store to keep it alive.
  Py_INCREF(store);
}

void PyFrame::Dealloc() {
  // Unlock tracking of handle in store.
  Unlink();

  // Release reference to store.
  Py_DECREF(store);
}

}  // namespace sling

