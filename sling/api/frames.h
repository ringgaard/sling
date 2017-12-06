#ifndef SLING_API_FRAMES_H_
#define SLING_API_FRAMES_H_

#include <vector>
#include <python2.7/Python.h>

#include "sling/frame/store.h"

namespace sling {

class PyFrame;

struct PyBase : public PyVarObject {
  PyObject *AsObject() { return reinterpret_cast<PyObject *>(this); }

  static void InitType(PyTypeObject *type,
                       const char *name, size_t size);
  static void RegisterType(PyTypeObject *type);
  static void RegisterType(PyTypeObject *type,
                           PyObject *module,
                           const char *name);
};

// Python wrapper for frame store.
struct PyStore : public PyBase {
  // Initialize new store.
  int Init(PyObject *args, PyObject *kwds);

  // Deallocate store.
  void Dealloc();

  // Freeze store.
  PyObject *Freeze();

  // Load frames from file.
  PyObject *Load(PyObject *args, PyObject *kw);

  // Save frames to file.
  PyObject *Save(PyObject *args, PyObject *kw);

  // Parse string as binary or ascii encoded frames.
  PyObject *Parse(PyObject *args, PyObject *kw);

  // Return the number of objects in the symbol table.
  Py_ssize_t Size();

  // Look up object in symbol table.
  PyObject *Lookup(PyObject *key);

  // Check if symbol is in store.
  int Contains(PyObject *key);

  // Return iterator for all symbols in symbol table.
  PyObject *Symbols();

  // Create new frame.
  PyObject *NewFrame(PyObject *arg);

  // Create new array.
  PyObject *NewArray(PyObject *arg);

  // Create new Python object for handle value.
  PyObject *PyValue(Handle handle);

  // Get handle value for Python object. Returns Handle::error() if the value
  // could not be converted.
  Handle Value(PyObject *object);

  // Get role handle value for Python object. This is similar to Value() except
  // that strings are considered to be symbol names. If existing=true then
  // nil will be returned if the symbol does not already exist.
  Handle RoleValue(PyObject *object, bool existing = false);

  // Get symbol handle value for Python object.
  Handle SymbolValue(PyObject *object);

  // Convert Python object to slot list. The Python object can either be a
  // dict or a list of 2-tuples.
  bool SlotList(PyObject *object, std::vector<Slot> *slots);

  // Underlying frame store.
  Store *store;

  // Registration.
  static PyTypeObject type;
  static PyMappingMethods mapping;
  static PySequenceMethods sequence;
  static PyMethodDef methods[];
  static void Define(PyObject *module);
};

// Python wrapper for symbol iterator.
struct PySymbols : public PyBase {
  // Initialize symbol iterator.
  void Init(PyStore *pystore);

  // Deallocate symbol iterator.
  void Dealloc();

  // Return next symbol.
  PyObject *Next();

  // Return self.
  PyObject *Self();

  // Store for symbols.
  PyStore *pystore;

  // Current symbol table bucket.
  int bucket;

  // Current symbol handle.
  Handle current;

  // Registration.
  static PyTypeObject type;
  static void Define(PyObject *module);
};

// Python wrapper for frame.
struct PyFrame : public PyBase, public Root {
  // Initialize frame wrapper.
  void Init(PyStore *pystore, Handle handle);

  // Deallocate frame wrapper.
  void Dealloc();

  // Return the number of slots in the frame.
  Py_ssize_t Size();

  // Look up role value for frame.
  PyObject *Lookup(PyObject *key);

  // Assign value to slot.
  int Assign(PyObject *key, PyObject *v);

  // Check if frame has role.
  int Contains(PyObject *key);

  // Get role value for frame wrapper.
  PyObject *GetAttr(PyObject *key);

  // Set role value for frame.
  int SetAttr(PyObject *key, PyObject *v);

  // Append slot to frame.
  PyObject *Append(PyObject *args);

  // Extend frame with list of slots.
  PyObject *Extend(PyObject *arg);

  // Return iterator for all slots in frame.
  PyObject *Slots();

  // Return iterator for finding all slots with a specific name.
  PyObject *Find(PyObject *args, PyObject *kw);

  // Return handle as hash value for frame.
  long Hash();

  // Check id frame is the same as another object.
  PyObject *Compare(PyObject *other, int op);

  // Return store for frame.
  PyObject *GetStore();

  // Return frame as string.
  PyObject *Str();

  // Return frame in ascii or binary encoding.
  PyObject *Data(PyObject *args, PyObject *kw);

  // Return handle for frame.
  Handle handle() const { return handle_; }

  // Dereference frame reference.
  FrameDatum *frame() { return pystore->store->Deref(handle())->AsFrame(); }

  // Store for frame.
  PyStore *pystore;

  // Registration.
  static PyTypeObject type;
  static PyMappingMethods mapping;
  static PySequenceMethods sequence;
  static PyMethodDef methods[];
  static void Define(PyObject *module);
};

// Python wrapper for frame slot iterator.
struct PySlots : public PyBase {
  // Initialize frame slot iterator.
  void Init(PyFrame *pyframe, Handle role);

  // Deallocate frame slot iterator.
  void Dealloc();

  // Return next (matching) slot.
  PyObject *Next();

  // Return self.
  PyObject *Self();

  // Frame that is being iterated.
  PyFrame *pyframe;

  // Current slot index.
  int current;

  // Slot role name or nil if all slots should be iterated.
  Handle role;

  // Registration.
  static PyTypeObject type;
  static void Define(PyObject *module);
};

// Python wrapper for array.
struct PyArray : public PyBase, public Root {
  // Initialize array wrapper.
  void Init(PyStore *pystore, Handle handle);

  // Deallocate array wrapper.
  void Dealloc();

  // Return the number of elements.
  Py_ssize_t Size();

  // Get element from array.
  PyObject *GetItem(Py_ssize_t index);

  // Set element in array.
  int SetItem(Py_ssize_t index, PyObject *value);

  // Return iterator for all items in array.
  PyObject *Items();

  // Return handle as hash value for array.
  long Hash();

  // Check if array contains value.
  int Contains(PyObject *key);

  // Return store for array.
  PyObject *GetStore();

  // Return array as string.
  PyObject *Str();

  // Return array in ascii or binary encoding.
  PyObject *Data(PyObject *args, PyObject *kw);

  // Return handle for array.
  Handle handle() const { return handle_; }

  // Dereference array reference.
  ArrayDatum *array() { return pystore->store->Deref(handle())->AsArray(); }

  // Store for frame.
  PyStore *pystore;

  // Registration.
  static PyTypeObject type;
  static PySequenceMethods sequence;
  static PyMethodDef methods[];
  static void Define(PyObject *module);
};

// Python wrapper for array item iterator.
struct PyItems : public PyBase {
  // Initialize array item iterator.
  void Init(PyArray *pyarray);

  // Deallocate array item iterator.
  void Dealloc();

  // Return next item.
  PyObject *Next();

  // Return self.
  PyObject *Self();

  // Array that is being iterated.
  PyArray *pyarray;

  // Current item.
  int current;

  // Registration.
  static PyTypeObject type;
  static void Define(PyObject *module);
};

}  // namespace sling

#endif  // SLING_API_FRAMES_H_

