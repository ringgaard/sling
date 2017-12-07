#include "sling/api/frames.h"

#include <python2.7/Python.h>

#include "sling/base/logging.h"
#include "sling/file/file.h"
#include "sling/frame/serialization.h"
#include "sling/frame/store.h"
#include "sling/stream/file.h"
#include "sling/stream/unix-file.h"

namespace sling {

// Python type declarations.
PyTypeObject PyStore::type;
PyMappingMethods PyStore::mapping;
PySequenceMethods PyStore::sequence;
PyTypeObject PySymbols::type;
PyTypeObject PyFrame::type;
PyMappingMethods PyFrame::mapping;
PySequenceMethods PyFrame::sequence;
PyTypeObject PySlots::type;
PyTypeObject PyArray::type;
PySequenceMethods PyArray::sequence;
PyTypeObject PyItems::type;

// Flags for serializing frames.
struct SerializationFlags {
  SerializationFlags(Store *store) {
    if (store->globals() == 0) global = true;
  }

  // Set flags for encoder.
  void InitEncoder(Encoder *encoder) {
    encoder->set_shallow(shallow);
    encoder->set_global(global);
  }

  // Set flags for printer.
  void InitPrinter(Printer *printer) {
    printer->set_indent(pretty ? 2 : 0);
    printer->set_shallow(shallow);
    printer->set_global(global);
    printer->set_byref(byref);
  }

  // Parse arguments for methods taking one argument.
  PyObject *ParseArgs(PyObject *args, PyObject *kw) {
    static const char *kwlist[] = {
      "file", "binary", "global", "shallow", "byref", "pretty", nullptr
    };
    PyObject *file = nullptr;
    bool ok = PyArg_ParseTupleAndKeywords(
                  args, kw, "O|bbbbb", const_cast<char **>(kwlist),
                  &file, &binary, &global, &shallow, &byref, &pretty);
    if (!ok) return nullptr;
    return file;
  }

  // Parse arguments for methods taking no fixed arguments.
  bool ParseFlags(PyObject *args, PyObject *kw) {
    static const char *kwlist[] = {
      "binary", "global", "shallow", "byref", "pretty", nullptr
    };
    return PyArg_ParseTupleAndKeywords(
        args, kw, "|bbbbb", const_cast<char **>(kwlist),
        &binary, &global, &shallow, &byref, &pretty);
  }

  bool binary = false;  // output in binary encoding
  bool global = false;  // output frames in the global store by value
  bool shallow = true;  // output frames with ids by reference
  bool byref = true;    // output anonymous frames by reference using index ids
  bool pretty = false;  // pretty print with indentation
};

void PyBase::InitType(PyTypeObject *type,
                      const char *name, size_t size) {
  type->tp_name = name;
  type->tp_basicsize = size;
  type->tp_new = PyType_GenericNew;
  type->tp_flags = Py_TPFLAGS_DEFAULT | Py_TPFLAGS_BASETYPE;
}

void PyBase::RegisterType(PyTypeObject *type) {
  PyType_Ready(type);
  Py_INCREF(type);
}

void PyBase::RegisterType(PyTypeObject *type,
                          PyObject *module,
                          const char *name) {
  PyType_Ready(type);
  Py_INCREF(type);
  PyModule_AddObject(module, name, reinterpret_cast<PyObject *>(type));
}

PyMethodDef PyStore::methods[] = {
  {"freeze", (PyCFunction) &PyStore::Freeze, METH_NOARGS, ""},
  {"load", (PyCFunction) &PyStore::Load, METH_VARARGS | METH_KEYWORDS, ""},
  {"save", (PyCFunction) &PyStore::Save, METH_VARARGS | METH_KEYWORDS, ""},
  {"parse", (PyCFunction) &PyStore::Parse, METH_VARARGS| METH_KEYWORDS, ""},
  {"frame", (PyCFunction) &PyStore::NewFrame, METH_O, ""},
  {"array", (PyCFunction) &PyStore::NewArray, METH_O, ""},
  {nullptr}
};

void PyStore::Define(PyObject *module) {
  InitType(&type, "sling.Store", sizeof(PyStore));

  type.tp_init = reinterpret_cast<initproc>(&PyStore::Init);
  type.tp_dealloc = reinterpret_cast<destructor>(&PyStore::Dealloc);
  type.tp_iter = &PyStore::Symbols;
  type.tp_methods = methods;

  type.tp_as_mapping = &mapping;
  mapping.mp_length = &PyStore::Size;
  mapping.mp_subscript = &PyStore::Lookup;

  type.tp_as_sequence = &sequence;
  sequence.sq_contains = &PyStore::Contains;

  RegisterType(&type, module, "Store");
}

int PyStore::Init(PyObject *args, PyObject *kwds) {
  // Get optional globals argument.
  PyStore *globals = nullptr;
  if (!PyArg_ParseTuple(args, "|O", &globals)) return -1;

  // Create new store.
  if (globals != nullptr) {
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
    // Create global store.
    store = new Store();
  }

  return 0;
}

void PyStore::Dealloc() {
  delete store;
}

PyObject *PyStore::Freeze() {
  if (store->globals() != nullptr) {
    PyErr_SetString(PyExc_ValueError, "Local store cannot be frozen");
    return nullptr;
  }
  store->Freeze();
  Py_RETURN_NONE;
}

PyObject *PyStore::Load(PyObject *args, PyObject *kw) {
  // Parse arguments.
  static const char *kwlist[] = {"file", "binary"};
  PyObject *file = nullptr;
  bool force_binary = false;
  bool ok = PyArg_ParseTupleAndKeywords(
                args, kw, "O|b", const_cast<char **>(kwlist),
                  &file, &force_binary);
  if (!ok) return nullptr;

  // Check that store is writable.
  if (!Writable()) return nullptr;

  // Read frames from file.
  if (PyFile_Check(file)) {
    // Load store from file object.
    StdFileInputStream stream(PyFile_AsFile(file), false);
    InputParser parser(store, &stream, force_binary);
    Object result = parser.ReadAll();
    if (parser.error()) {
      PyErr_SetString(PyExc_IOError, parser.error_message().c_str());
      return nullptr;
    }
    return PyValue(result.handle());
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
    InputParser parser(store, &stream, force_binary);
    Object result = parser.ReadAll();
    if (parser.error()) {
      PyErr_SetString(PyExc_IOError, parser.error_message().c_str());
      return nullptr;
    }
    return PyValue(result.handle());
  } else {
    PyErr_SetString(PyExc_ValueError, "File or string argument expected");
    return nullptr;
  }
}

PyObject *PyStore::Save(PyObject *args, PyObject *kw) {
  // Get arguments.
  SerializationFlags flags(store);
  PyObject *file = flags.ParseArgs(args, kw);

  // Get output stream.
  OutputStream *stream;
  if (PyFile_Check(file)) {
    // Create stream from stdio file.
    stream = new StdFileOutputStream(PyFile_AsFile(file), false);
  } else if (PyString_Check(file)) {
    // Open output file.
    File *f;
    Status st = File::Open(PyString_AsString(file), "w", &f);
    if (!st.ok()) {
      PyErr_SetString(PyExc_IOError, st.message());
      return nullptr;
    }
    stream = new FileOutputStream(f);
  } else {
    PyErr_SetString(PyExc_ValueError, "File or string argument expected");
    return nullptr;
  }

  // Write frames to output.
  Output output(stream);
  if (flags.binary) {
    Encoder encoder(store, &output);
    flags.InitEncoder(&encoder);
    encoder.EncodeAll();
  } else {
    Printer printer(store, &output);
    flags.InitPrinter(&printer);
    printer.PrintAll();
  }

  output.Flush();
  delete stream;
  Py_RETURN_NONE;
}

PyObject *PyStore::Parse(PyObject *args, PyObject *kw) {
  // Parse arguments.
  static const char *kwlist[] = {"data", "binary"};
  PyObject *object = nullptr;
  bool force_binary = false;
  bool ok = PyArg_ParseTupleAndKeywords(
                args, kw, "S|b", const_cast<char **>(kwlist),
                  &object, &force_binary);
  if (!ok) return nullptr;

  // Check that store is writable.
  if (!Writable()) return nullptr;

  // Get data buffer.
  char *data;
  Py_ssize_t length;
  PyString_AsStringAndSize(object, &data, &length);

  // Load frames from memory buffer.
  ArrayInputStream stream(data, length);
  InputParser parser(store, &stream);
  Object result = parser.ReadAll();
  if (parser.error()) {
    PyErr_SetString(PyExc_IOError, parser.error_message().c_str());
    return nullptr;
  }
  return PyValue(result.handle());
}

Py_ssize_t PyStore::Size() {
  return store->num_symbols();
}

PyObject *PyStore::Lookup(PyObject *key) {
  // Get symbol name.
  char *name = PyString_AsString(key);
  if (name == nullptr) return nullptr;

  // Lookup name in symbol table.
  Handle handle = store->Lookup(name);
  return PyValue(handle);
}

int PyStore::Contains(PyObject *key) {
  // Get symbol name.
  char *name = PyString_AsString(key);
  if (name == nullptr) return -1;

  // Lookup name in symbol table.
  Handle handle = store->LookupExisting(name);
  return !handle.IsNil();
}

PyObject *PyStore::Symbols() {
  PySymbols *iter = PyObject_New(PySymbols, &PySymbols::type);
  iter->Init(this);
  return iter->AsObject();
}

PyObject *PyStore::NewFrame(PyObject *arg) {
  // Check that store is writable.
  if (!Writable()) return nullptr;

  // Parse data into slot list.
  GCLock lock(store);
  std::vector<Slot> slots;
  if (!SlotList(arg, &slots)) return nullptr;

  // Allocate new frame.
  Slot *begin = slots.data();
  Slot *end = slots.data() + slots.size();
  Handle handle = store->AllocateFrame(begin, end);

  // Return new frame wrapper for handle.
  PyFrame *frame = PyObject_New(PyFrame, &PyFrame::type);
  frame->Init(this, handle);
  return frame->AsObject();
}

PyObject *PyStore::NewArray(PyObject *arg) {
  // Check that store is writable.
  if (!Writable()) return nullptr;

  GCLock lock(store);
  Handle handle;
  if (PyList_Check(arg)) {
    // Inialize new array from Python list.
    int size = PyList_Size(arg);
    handle = store->AllocateArray(size);
    ArrayDatum *array = store->Deref(handle)->AsArray();
    for (int i = 0; i < size; ++i) {
      PyObject *item = PyList_GetItem(arg, i);
      Handle value = Value(item);
      if (value.IsError()) return nullptr;
      *array->at(i) = value;
    }
  } else {
    int size = PyInt_AsLong(arg);
    if (size < 0) return nullptr;
    handle = store->AllocateArray(size);
  }

  // Return array wrapper for new array.
  PyArray *array = PyObject_New(PyArray, &PyArray::type);
  array->Init(this, handle);
  return array->AsObject();
}

bool PyStore::Writable() {
  if (store->frozen()) {
    PyErr_SetString(PyExc_ValueError, "Frame store is not writable");
    return false;
  }
  return true;
}

PyObject *PyStore::PyValue(Handle handle) {
  switch (handle.tag()) {
    case Handle::kGlobal:
    case Handle::kLocal: {
      // Return None for nil.
      if (handle.IsNil()) Py_RETURN_NONE;

      // Get datum for object.
      Datum *datum = store->Deref(handle);

      if (datum->IsFrame()) {
        // Return new frame wrapper for handle.
        PyFrame *frame = PyObject_New(PyFrame, &PyFrame::type);
        frame->Init(this, handle);
        return frame->AsObject();
      } else if (datum->IsString()) {
        // Return string object.
        StringDatum *str = datum->AsString();
        return PyString_FromStringAndSize(str->data(), str->size());
      } else if (datum->IsArray()) {
        // Return new frame array for handle.
        PyArray *array = PyObject_New(PyArray, &PyArray::type);
        array->Init(this, handle);
        return array->AsObject();
      } else if (datum->IsSymbol()) {
        // Return symbol name.
        SymbolDatum *symbol = datum->AsSymbol();
        StringDatum *str = store->Deref(symbol->name)->AsString();
        return PyString_FromStringAndSize(str->data(), str->size());
      } else {
        // Unsupported type.
        PyErr_SetString(PyExc_ValueError, "Unsupported object type");
        return nullptr;
      }
    }

    case Handle::kIntTag:
      // Return integer object.
      return PyInt_FromLong(handle.AsInt());

    case Handle::kFloatTag:
      // Return floating point number object.
      return PyFloat_FromDouble(handle.AsFloat());
  }

  return nullptr;
}

Handle PyStore::Value(PyObject *object) {
  if (object == Py_None) {
    return Handle::nil();
  } else if (PyObject_TypeCheck(object, &PyFrame::type)) {
    // Return handle for frame.
    PyFrame *frame = reinterpret_cast<PyFrame *>(object);
    if (frame->pystore->store != store &&
        frame->pystore->store != store->globals()) {
      PyErr_SetString(PyExc_ValueError, "Frame does not belongs to this store");
      return Handle::error();
    }
    return frame->handle();
  } else if (PyString_Check(object)) {
    // Create string and return handle.
    char *data;
    Py_ssize_t length;
    PyString_AsStringAndSize(object, &data, &length);
    return  store->AllocateString(Text(data, length));
  } else if (PyInt_Check(object)) {
    // Return integer handle.
    return Handle::Integer(PyInt_AsLong(object));
  } else if (PyFloat_Check(object)) {
    // Return floating point number handle.
    return Handle::Float(PyFloat_AsDouble(object));
  } else if (PyObject_TypeCheck(object, &PyArray::type)) {
    // Return handle for frame.
    PyArray *array = reinterpret_cast<PyArray *>(object);
    if (array->pystore->store != store &&
        array->pystore->store != store->globals()) {
      PyErr_SetString(PyExc_ValueError, "Array does not belongs to this store");
      return Handle::error();
    }
    return array->handle();
  } else {
    PyErr_SetString(PyExc_ValueError, "Unsupported frame value type");
    return Handle::error();
  }
}

Handle PyStore::RoleValue(PyObject *object, bool existing) {
  if (PyString_Check(object)) {
    char *name = PyString_AsString(object);
    if (name == nullptr) return Handle::error();
    if (existing) {
      return store->LookupExisting(name);
    } else {
      return store->Lookup(name);
    }
  } else {
    return Value(object);
  }
}

Handle PyStore::SymbolValue(PyObject *object) {
  if (PyString_Check(object)) {
    char *name = PyString_AsString(object);
    if (name == nullptr) return Handle::error();
    return store->Symbol(name);
  } else {
    return Value(object);
  }
}

bool PyStore::SlotList(PyObject *object, std::vector<Slot> *slots) {
  if (PyDict_Check(object)) {
    // Build slots from key/value pairs in dictionary.
    PyObject *k;
    PyObject *v;
    Py_ssize_t pos = 0;
    slots->reserve(slots->size() + PyDict_Size(object));
    while (PyDict_Next(object, &pos, &k, &v)) {
      // Get slot name.
      Handle name = RoleValue(k);
      if (name.IsError()) return false;

      // Get slot value.
      Handle value;
      if (name.IsId() && PyString_Check(v)) {
        value = SymbolValue(v);
      } else {
        value = Value(v);
      }
      if (value.IsError()) return false;

      // Add slot.
      slots->emplace_back(name, value);
    }
  } else if (PyList_Check(object)) {
    // Build slots from list of 2-tuples.
    int size = PyList_Size(object);
    slots->reserve(slots->size() + size);
    for (int i = 0; i < size; ++i) {
      // Check that item is a 2-tuple.
      PyObject *item = PyList_GetItem(object, i);
      if (!PyTuple_Check(item) ||  PyTuple_Size(item) != 2) {
        PyErr_SetString(PyExc_ValueError, "Slot list must contain 2-tuples");
        return false;
      }

      // Get slot name.
      PyObject *k = PyTuple_GetItem(item, 0);
      Handle name = RoleValue(k);
      if (name.IsError()) return false;

      // Get slot value.
      PyObject *v = PyTuple_GetItem(item, 1);
      Handle value;
      if (name.IsId() && PyString_Check(v)) {
        value = SymbolValue(v);
      } else {
        value = Value(v);
      }
      if (value.IsError()) return false;

      // Add slot.
      slots->emplace_back(name, value);
    }
  } else {
    return false;
  }
  return true;
}

void PySymbols::Define(PyObject *module) {
  InitType(&type, "sling.Symbols", sizeof(PySymbols));
  type.tp_dealloc = reinterpret_cast<destructor>(&PySymbols::Dealloc);
  type.tp_iter = &PySymbols::Self;
  type.tp_iternext = &PySymbols::Next;
  RegisterType(&type);
}

void PySymbols::Init(PyStore *pystore) {
  // Initialize iterator.
  bucket = -1;
  current = Handle::nil();

  // Add reference to store to keep it alive.
  this->pystore = pystore;
  Py_INCREF(pystore);
}

void PySymbols::Dealloc() {
  // Release reference to store.
  Py_DECREF(pystore);
}

PyObject *PySymbols::Next() {
  // Get next bucket if needed.
  if (current.IsNil()) {
    MapDatum *symbols = pystore->store->GetMap(pystore->store->symbols());
    while (current.IsNil()) {
      if (++bucket >= symbols->length()) {
        PyErr_SetNone(PyExc_StopIteration);
        return nullptr;
      }
      current = symbols->get(bucket);
    }
  }

  // Get next symbol in bucket.
  SymbolDatum *symbol = pystore->store->Deref(current)->AsSymbol();
  current = symbol->next;
  return pystore->PyValue(symbol->value);
}

PyObject *PySymbols::Self() {
  Py_INCREF(this);
  return AsObject();
}

PyMethodDef PyFrame::methods[] = {
  {"data", (PyCFunction) &PyFrame::Data, METH_KEYWORDS, ""},
  {"append", (PyCFunction) &PyFrame::Append, METH_VARARGS, ""},
  {"extend", (PyCFunction) &PyFrame::Extend, METH_O, ""},
  {"store", (PyCFunction) &PyFrame::GetStore, METH_NOARGS, ""},
  {nullptr}
};

void PyFrame::Define(PyObject *module) {
  InitType(&type, "sling.Frame", sizeof(PyFrame));
  type.tp_dealloc = reinterpret_cast<destructor>(&PyFrame::Dealloc);
  type.tp_getattro = &PyFrame::GetAttr;
  type.tp_setattro = &PyFrame::SetAttr;
  type.tp_str = &PyFrame::Str;
  type.tp_iter = &PyFrame::Slots;
  type.tp_call = &PyFrame::Find;
  type.tp_hash = &PyFrame::Hash;
  type.tp_richcompare = &PyFrame::Compare;
  type.tp_methods = methods;

  type.tp_as_mapping = &mapping;
  mapping.mp_length = &PyFrame::Size;
  mapping.mp_subscript = &PyFrame::Lookup;
  mapping.mp_ass_subscript = &PyFrame::Assign;

  type.tp_as_sequence = &sequence;
  sequence.sq_contains = &PyFrame::Contains;

  RegisterType(&type);
}

void PyFrame::Init(PyStore *pystore, Handle handle) {
  // Add frame as root object for store to keep it alive in the store.
  InitRoot(pystore->store, handle);

  // Add reference to store to keep it alive.
  this->pystore = pystore;
  Py_INCREF(pystore);
}

void PyFrame::Dealloc() {
  // Unlock tracking of handle in store.
  Unlink();

  // Release reference to store.
  Py_DECREF(pystore);
}

Py_ssize_t PyFrame::Size() {
  return frame()->slots();
}

long PyFrame::Hash() {
  return handle().bits;
}

PyObject *PyFrame::Compare(PyObject *other, int op) {
  // Only equality check is suported.
  if (op != Py_EQ && op != Py_NE) {
    PyErr_SetString(PyExc_TypeError, "Invalid frame comparison");
    return nullptr;
  }

  // Check if other object is a frame.
  bool match = false;
  if (PyObject_TypeCheck(other, &PyFrame::type)) {
    // Check if the stores and handles are the same.
    PyFrame *pyother = reinterpret_cast<PyFrame *>(other);
    match = pyother->pystore->store == pystore->store &&
            pyother->handle() == handle();
  }

  if (op == Py_NE) match = !match;
  if (match) {
    Py_RETURN_TRUE;
  } else {
    Py_RETURN_FALSE;
  }
}

PyObject *PyFrame::GetStore() {
  Py_INCREF(pystore);
  return pystore->AsObject();
}

PyObject *PyFrame::Lookup(PyObject *key) {
  // Look up role.
  Handle role = pystore->RoleValue(key, true);
  if (role.IsError()) return nullptr;

  // Return None if the role name does not exist.
  if (role.IsNil()) Py_RETURN_NONE;

  // Look up (first) value for role.
  Handle value = frame()->get(role);
  return pystore->PyValue(value);
}

int PyFrame::Assign(PyObject *key, PyObject *v) {
  // Check that frame is writable.
  if (!Writable()) return -1;

  // Look up role.
  GCLock lock(pystore->store);
  Handle role = pystore->RoleValue(key);
  if (role.IsError()) return -1;

  // Get new frame role value.
  Handle value = pystore->Value(v);
  if (value.IsError()) return -1;

  // Check role.
  if (role.IsNil()) {
    PyErr_SetString(PyExc_IndexError, "Role not defined");
    return -1;
  };
  if (role == Handle::id()) {
    PyErr_SetString(PyExc_IndexError, "Frame id cannot be changed");
    return -1;
  };

  // Set frame slot value.
  pystore->store->Set(handle(), role, value);

  return 0;
}

int PyFrame::Contains(PyObject *key) {
  // Look up role.
  Handle role = pystore->RoleValue(key, true);
  if (role.IsError()) return -1;

  // Check if frame has slot with role.
  if (role.IsNil()) return 0;
  return frame()->has(role);
}

PyObject *PyFrame::GetAttr(PyObject *key) {
  // Get attribute name.
  char *name = PyString_AsString(key);
  if (name == nullptr) return nullptr;

  // Resolve methods.
  PyObject *method = Py_FindMethod(methods, AsObject(), name);
  if (method != nullptr) return method;
  PyErr_Clear();

  // Lookup role.
  Handle role = pystore->store->LookupExisting(name);
  if (role.IsNil()) Py_RETURN_NONE;

  // Get role value for frame.
  Handle value = frame()->get(role);
  return pystore->PyValue(value);
}

int PyFrame::SetAttr(PyObject *key, PyObject *v) {
  // Check that frame is writable.
  if (!Writable()) return -1;

  // Get role name.
  char *name = PyString_AsString(key);
  if (name == nullptr) return -1;

  // Lookup role.
  GCLock lock(pystore->store);
  Handle role = pystore->store->Lookup(name);
  if (role.IsNil()) {
    PyErr_SetString(PyExc_IndexError, "Role not defined");
    return -1;
  };
  if (role == Handle::id()) {
    PyErr_SetString(PyExc_IndexError, "Frame id cannot be changed");
    return -1;
  };

  // Get role value.
  Handle value = pystore->Value(v);
  if (value.IsError())  return -1;

  // Set role value for frame.
  pystore->store->Set(handle(), role, value);

  return 0;
}

PyObject *PyFrame::Append(PyObject *args) {
  // Check that frame is writable.
  if (!Writable()) return nullptr;

  // Get name and value arguments.
  PyObject *pyname;
  PyObject *pyvalue;
  if (!PyArg_ParseTuple(args, "OO", &pyname, &pyvalue)) return nullptr;

  // Get role.
  GCLock lock(pystore->store);
  Handle role = pystore->RoleValue(pyname);
  if (role.IsError()) return nullptr;

  // Get value.
  Handle value = pystore->Value(pyvalue);
  if (value.IsError()) return nullptr;

  // Set role value for frame.
  pystore->store->Set(handle(), role, value);
  Py_RETURN_NONE;
}

PyObject *PyFrame::Extend(PyObject *arg) {
  // Check that frame is writable.
  if (!Writable()) return nullptr;

  // Get existing slots for frame.
  GCLock lock(pystore->store);
  std::vector<Slot> slots(frame()->begin(), frame()->end());

  // Append slots from data to slot list.
  if (!pystore->SlotList(arg, &slots)) return nullptr;

  // Reallocate frame.
  Slot *begin = slots.data();
  Slot *end = slots.data() + slots.size();
  handle_ = pystore->store->AllocateFrame(begin, end, handle_);
  Py_RETURN_NONE;
}

PyObject *PyFrame::Slots() {
  PySlots *iter = PyObject_New(PySlots, &PySlots::type);
  iter->Init(this, Handle::nil());
  return iter->AsObject();
}

PyObject *PyFrame::Find(PyObject *args, PyObject *kw) {
  // Get role argument.
  PyObject *pyrole;
  if (!PyArg_ParseTuple(args, "O", &pyrole)) return nullptr;
  Handle role = pystore->RoleValue(pyrole);
  if (role.IsError()) return nullptr;

  // Create iterator for find all slot with the role.
  PySlots *iter = PyObject_New(PySlots, &PySlots::type);
  iter->Init(this, role);
  return iter->AsObject();
}

PyObject *PyFrame::Str() {
  FrameDatum *f = frame();
  if (f->IsNamed()) {
    // Return frame id.
    Handle id = f->get(Handle::id());
    SymbolDatum *symbol = pystore->store->Deref(id)->AsSymbol();
    StringDatum *name = pystore->store->GetString(symbol->name);
    return PyString_FromStringAndSize(name->data(), name->size());
  } else {
    // Return frame as text.
    StringPrinter printer(pystore->store);
    printer.Print(handle());
    const string &text = printer.text();
    return PyString_FromStringAndSize(text.data(), text.size());
  }
}

PyObject *PyFrame::Data(PyObject *args, PyObject *kw) {
  // Get arguments.
  SerializationFlags flags(pystore->store);
  if (!flags.ParseFlags(args, kw)) return nullptr;

  // Serialize frame.
  if (flags.binary) {
    StringEncoder encoder(pystore->store);
    flags.InitEncoder(encoder.encoder());
    encoder.Encode(handle());
    const string &buffer = encoder.buffer();
    return PyString_FromStringAndSize(buffer.data(), buffer.size());
  } else {
    StringPrinter printer(pystore->store);
    flags.InitPrinter(printer.printer());
    printer.Print(handle());
    const string &text = printer.text();
    return PyString_FromStringAndSize(text.data(), text.size());
  }
}

bool PyFrame::Writable() {
  if (pystore->store->frozen() || !pystore->store->Owned(handle())) {
    PyErr_SetString(PyExc_ValueError, "Frame is not writable");
    return false;
  }
  return true;
}

void PySlots::Define(PyObject *module) {
  InitType(&type, "sling.Slots", sizeof(PySlots));
  type.tp_dealloc = reinterpret_cast<destructor>(&PySlots::Dealloc);
  type.tp_iter = &PySlots::Self;
  type.tp_iternext = &PySlots::Next;
  RegisterType(&type);
}

void PySlots::Init(PyFrame *pyframe, Handle role) {
  current = -1;
  this->pyframe = pyframe;
  this->role = role;
  Py_INCREF(pyframe);
}

void PySlots::Dealloc() {
  Py_DECREF(pyframe);
}

PyObject *PySlots::Next() {
  // Check if there are any more slots.
  FrameDatum *f = pyframe->frame();
  while (++current < f->slots()) {
    // Check for role match.
    Slot *slot = f->begin() + current;
    if (role.IsNil()) {
      // Create two-tuple for name and value.
      PyObject *name = pyframe->pystore->PyValue(slot->name);
      PyObject *value = pyframe->pystore->PyValue(slot->value);
      return PyTuple_Pack(2, name, value);
    } else if (role == slot->name) {
      return pyframe->pystore->PyValue(slot->value);
    }
  }

  // More more slots.
  PyErr_SetNone(PyExc_StopIteration);
  return nullptr;
}

PyObject *PySlots::Self() {
  Py_INCREF(this);
  return AsObject();
}

PyMethodDef PyArray::methods[] = {
  {"store", (PyCFunction) &PyArray::GetStore, METH_NOARGS, ""},
  {"data", (PyCFunction) &PyFrame::Data, METH_KEYWORDS, ""},
  {nullptr}
};

void PyArray::Define(PyObject *module) {
  InitType(&type, "sling.Array", sizeof(PyArray));
  type.tp_dealloc = reinterpret_cast<destructor>(&PyArray::Dealloc);
  type.tp_str = &PyArray::Str;
  type.tp_iter = &PyArray::Items;
  type.tp_hash = &PyArray::Hash;
  type.tp_methods = methods;

  type.tp_as_sequence = &sequence;
  sequence.sq_length = &PyArray::Size;
  sequence.sq_item = &PyArray::GetItem;
  sequence.sq_ass_item = &PyArray::SetItem;
  sequence.sq_contains = &PyArray::Contains;

  RegisterType(&type);
}

void PyArray::Init(PyStore *pystore, Handle handle) {
  // Add array as root object for store to keep it alive in the store.
  InitRoot(pystore->store, handle);

  // Add reference to store to keep it alive.
  this->pystore = pystore;
  Py_INCREF(pystore);
}

void PyArray::Dealloc() {
  // Unlock tracking of handle in store.
  Unlink();

  // Release reference to store.
  Py_DECREF(pystore);
}

Py_ssize_t PyArray::Size() {
  return array()->length();
}

PyObject *PyArray::GetItem(Py_ssize_t index) {
  // Check array bounds.
  ArrayDatum *arr = array();
  if (index < 0 || index >= arr->length()) {
    PyErr_SetString(PyExc_IndexError, "Array index out of bounds");
    return nullptr;
  }

  // Return array element.
  return pystore->PyValue(arr->get(index));
}

int PyArray::SetItem(Py_ssize_t index, PyObject *value) {
  // Check that array is writable.
  if (!Writable()) return -1;

  // Check array bounds.
  if (index < 0 || index >= array()->length()) {
    PyErr_SetString(PyExc_IndexError, "Array index out of bounds");
    return -1;
  }

  // Set array element.
  Handle handle = pystore->Value(value);
  if (handle.IsError()) return -1;
  *array()->at(index) = handle;
  return 0;
}

PyObject *PyArray::Items() {
  PyItems *iter = PyObject_New(PyItems, &PyItems::type);
  iter->Init(this);
  return iter->AsObject();
}

long PyArray::Hash() {
  return handle().bits;
}

int PyArray::Contains(PyObject *key) {
  // Get handle for key.
  Handle handle = pystore->Value(key);
  if (handle.IsError()) return -1;

  // Check if value is contained in array.
  ArrayDatum *arr = array();
  for (int i = 0; i < arr->length(); ++i) {
    if (arr->get(i) == handle) return true;
  }
  return false;
}

PyObject *PyArray::GetStore() {
  Py_INCREF(pystore);
  return pystore->AsObject();
}

PyObject *PyArray::Str() {
  StringPrinter printer(pystore->store);
  printer.Print(handle());
  const string &text = printer.text();
  return PyString_FromStringAndSize(text.data(), text.size());
}

PyObject *PyArray::Data(PyObject *args, PyObject *kw) {
  // Get arguments.
  SerializationFlags flags(pystore->store);
  if (!flags.ParseFlags(args, kw)) return nullptr;

  // Serialize frame.
  if (flags.binary) {
    StringEncoder encoder(pystore->store);
    flags.InitEncoder(encoder.encoder());
    encoder.Encode(handle());
    const string &buffer = encoder.buffer();
    return PyString_FromStringAndSize(buffer.data(), buffer.size());
  } else {
    StringPrinter printer(pystore->store);
    flags.InitPrinter(printer.printer());
    printer.Print(handle());
    const string &text = printer.text();
    return PyString_FromStringAndSize(text.data(), text.size());
  }
}

bool PyArray::Writable() {
  if (pystore->store->frozen() || !pystore->store->Owned(handle())) {
    PyErr_SetString(PyExc_ValueError, "Array is not writable");
    return false;
  }
  return true;
}

void PyItems::Define(PyObject *module) {
  InitType(&type, "sling.Items", sizeof(PyItems));
  type.tp_dealloc = reinterpret_cast<destructor>(&PyItems::Dealloc);
  type.tp_iter = &PyItems::Self;
  type.tp_iternext = &PyItems::Next;
  RegisterType(&type);
}

void PyItems::Init(PyArray *pyarray) {
  current = -1;
  this->pyarray = pyarray;
  Py_INCREF(pyarray);
}

void PyItems::Dealloc() {
  // Release reference to array.
  Py_DECREF(pyarray);
}

PyObject *PyItems::Next() {
  // Check bounds.
  ArrayDatum *arr = pyarray->array();
  if (++current >= arr->length()) {
    PyErr_SetNone(PyExc_StopIteration);
    return nullptr;
  }

  // Get next item in array.
  return pyarray->pystore->PyValue(arr->get(current));
}

PyObject *PyItems::Self() {
  Py_INCREF(this);
  return AsObject();
}

}  // namespace sling

