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

#include "sling/pyapi/pydatabase.h"

namespace sling {

// Python type declarations.
PyTypeObject PyDatabase::type;
PyMethodTable PyDatabase::methods;
PyMappingMethods PyDatabase::mapping;
PySequenceMethods PyDatabase::sequence;
PyTypeObject PyCursor::type;

// Check status.
static bool CheckIO(Status status) {
  bool ok = status.ok();
  if (!ok) PyErr_SetString(PyExc_IOError, status.message());
  return ok;
}

void PyDatabase::Define(PyObject *module) {
  InitType(&type, "sling.Database", sizeof(PyDatabase), true);

  type.tp_init = method_cast<initproc>(&PyDatabase::Init);
  type.tp_dealloc = method_cast<destructor>(&PyDatabase::Dealloc);
  type.tp_call = method_cast<ternaryfunc>(&PyDatabase::Full);
  type.tp_iter = method_cast<getiterfunc>(&PyDatabase::Iterator);

  type.tp_as_mapping = &mapping;
  mapping.mp_subscript = method_cast<binaryfunc>(&PyDatabase::Lookup);
  mapping.mp_ass_subscript = method_cast<objobjargproc>(&PyDatabase::Assign);

  type.tp_as_sequence = &sequence;
  sequence.sq_contains = method_cast<objobjproc>(&PyDatabase::Contains);

  methods.Add("close", &PyDatabase::Close);
  methods.AddO("get", &PyDatabase::Get);
  methods.Add("put", &PyDatabase::Put);
  methods.Add("add", &PyDatabase::Add);
  methods.AddO("delete", &PyDatabase::Delete);
  methods.Add("keys", &PyDatabase::Keys);
  methods.Add("values", &PyDatabase::Values);
  methods.Add("items", &PyDatabase::Items);
  methods.Add("position", &PyDatabase::Position);
  methods.Add("epoch", &PyDatabase::Epoch);
  type.tp_methods = methods.table();

  RegisterType(&type, module, "Database");

  RegisterEnum(module, "DBOVERWRITE", DBOVERWRITE);
  RegisterEnum(module, "DBADD", DBADD);
  RegisterEnum(module, "DBORDERED", DBORDERED);
  RegisterEnum(module, "DBNEWER", DBNEWER);

  RegisterEnum(module, "DBNEW", DBNEW);
  RegisterEnum(module, "DBUPDATED", DBUPDATED);
  RegisterEnum(module, "DBUNCHANGED", DBUNCHANGED);
  RegisterEnum(module, "DBEXISTS", DBEXISTS);
  RegisterEnum(module, "DBSTALE", DBSTALE);
  RegisterEnum(module, "DBFAULT", DBFAULT);
}

int PyDatabase::Init(PyObject *args, PyObject *kwds) {
  // Initialize defaults.
  this->batchsize = 128;
  this->position = 0;
  this->mu = new Mutex();

  // Get arguments.
  static const char *kwlist[] = {"database", "agent", "batch", nullptr};
  char *dbname;
  char *agent = "";
  bool ok = PyArg_ParseTupleAndKeywords(
                args, kwds, "s|si", const_cast<char **>(kwlist),
                &dbname, &agent, &this->batchsize);
  if (!ok) return -1;

  // Open connection to database.
  db = new DBClient();
  if (!CheckIO(db->Connect(dbname, agent))) return -1;

  return 0;
}

void PyDatabase::Dealloc() {
  delete mu;
  delete db;
  Free();
}

Status PyDatabase::Transact(Transaction tx) {
  // Release Python GIL to allow other Python threads to execute concurrently
  // with the database operation.
  PyThreadState *state = PyEval_SaveThread();

  // Execute operation.
  mu->Lock();
  Status st = tx();
  mu->Unlock();

  // Acquire Python GIL again.
  PyEval_RestoreThread(state);

  return st;
}

PyObject *PyDatabase::Close() {
  if (!CheckIO(db->Close())) return nullptr;
  Py_RETURN_NONE;
}

PyObject *PyDatabase::Get(PyObject *obj) {
  // Get record key.
  Slice key;
  if (!GetData(obj, &key)) return nullptr;

  // Fetch record.
  IOBuffer buffer;
  DBRecord record;
  Status st = Transact([&]() -> Status {
    return db->Get(key, &record, &buffer);
  });
  if (!CheckIO(st)) return nullptr;

  // Return tuple with value and version.
  PyObject *value = PyValue(record.value);
  PyObject *version = PyLong_FromLong(record.version);
  PyObject *pair = PyTuple_Pack(2, value, version);
  Py_DECREF(value);
  Py_DECREF(version);
  return pair;
}

PyObject *PyDatabase::Put(PyObject *args, PyObject *kw) {
  // Parse arguments.
  static const char *kwlist[] = {"key", "value", "version", "mode", nullptr};
  DBRecord record;
  PyObject *key = nullptr;
  PyObject *value = nullptr;
  DBMode mode = DBOVERWRITE;
  bool ok = PyArg_ParseTupleAndKeywords(
                args, kw, "OO|li", const_cast<char **>(kwlist),
                &key, &value, &record.version, &mode);
  if (!ok) return nullptr;
  if (!GetData(key, &record.key)) return nullptr;
  if (!GetData(value, &record.value)) return nullptr;

  // Update record in database.
  Status st = Transact([&]() -> Status {
    return db->Put(&record, mode);
  });
  if (!CheckIO(st)) return nullptr;

  // Return outcome.
  return PyLong_FromLong(record.result);
}

PyObject *PyDatabase::Add(PyObject *args, PyObject *kw) {
  // Parse arguments.
  static const char *kwlist[] = {"key", "value", "version", nullptr};
  DBRecord record;
  PyObject *key = nullptr;
  PyObject *value = nullptr;
  bool ok = PyArg_ParseTupleAndKeywords(
                args, kw, "OO|l", const_cast<char **>(kwlist),
                &key, &value, &record.version);
  if (!ok) return nullptr;
  if (!GetData(key, &record.key)) return nullptr;
  if (!GetData(value, &record.value)) return nullptr;

  // Update record in database.
  Status st = Transact([&]() -> Status {
    return db->Add(&record);
  });
  if (!CheckIO(st)) return nullptr;

  // Return outcome.
  return PyLong_FromLong(record.result);
}

PyObject *PyDatabase::Delete(PyObject *key) {
  Slice k;
  if (!GetData(key, &k)) return nullptr;

  Status st = Transact([&]() -> Status {
    return db->Delete(k);
  });
  if (!CheckIO(st)) return nullptr;

  Py_RETURN_NONE;
}

int PyDatabase::Contains(PyObject *key) {
  // Get key.
  Slice k;
  if (!GetData(key, &k)) return -1;

  // Check record in database.
  DBRecord record;
  bool exists = false;
  Status st = Transact([&]() -> Status {
    Status s = db->Head(k, &record);
    exists = !record.value.empty();
    return s;
  });
  if (!CheckIO(st)) return -1;

  // Record exists if size is not zero.
  return exists;
}

PyObject *PyDatabase::Lookup(PyObject *key) {
  // Get record key.
  DBRecord record;
  if (!GetData(key, &record.key)) return nullptr;

  // Fetch record.
  IOBuffer buffer;
  Status st = Transact([&]() -> Status {
    return db->Get(record.key, &record, &buffer);
  });
  if (!CheckIO(st)) return nullptr;

  // Return record value.
  return PyValue(record.value);
}

int PyDatabase::Assign(PyObject *key, PyObject *v) {
  if (v == nullptr) {
    // Delete record from database.
    Slice k;
    if (!GetData(key, &k)) return -1;

    Status st = Transact([&]() -> Status {
      return db->Delete(k);
    });
    if (!CheckIO(st)) return -1;
  } else {
    // Update/add record in database.
    DBRecord record;
    if (!GetData(key, &record.key)) return -1;
    if (!GetData(v, &record.value)) return -1;

    Status st = Transact([&]() -> Status {
      return db->Put(&record);
    });
    if (!CheckIO(st)) return -1;
  }

  return 0;
}

PyObject *PyDatabase::Iterator() {
  return PyCursor::Create(this, PyCursor::FULL, nullptr, nullptr);
}

PyObject *PyDatabase::Keys(PyObject *args, PyObject *kw) {
  return PyCursor::Create(this, PyCursor::KEYS, args, kw);
}

PyObject *PyDatabase::Values(PyObject *args, PyObject *kw) {
  return PyCursor::Create(this, PyCursor::VALUES, args, kw);
}

PyObject *PyDatabase::Items(PyObject *args, PyObject *kw) {
  return PyCursor::Create(this, PyCursor::ITEMS, args, kw);
}

PyObject *PyDatabase::Full(PyObject *args, PyObject *kw) {
  return PyCursor::Create(this, PyCursor::FULL, args, kw);
}

PyObject *PyDatabase::Position() {
  return PyLong_FromLong(position);
}

PyObject *PyDatabase::Epoch() {
  uint64 epoch;
  Status st = Transact([&]() -> Status {
    return db->Epoch(&epoch);
  });
  if (!CheckIO(st)) return nullptr;


  return PyLong_FromLong(epoch);
}

bool PyDatabase::GetData(PyObject *obj, Slice *data) {
  char *buffer;
  Py_ssize_t length;

  if (PyBytes_Check(obj)) {
    if (PyBytes_AsStringAndSize(obj, &buffer, &length) == -1) return false;
  } else {
    buffer = PyUnicode_AsUTF8AndSize(obj, &length);
    if (buffer == nullptr) return false;
  }

  *data = Slice(buffer, length);
  return true;
}

PyObject *PyDatabase::PyValue(const Slice &slice, bool binary) {
  if (slice.empty()) Py_RETURN_NONE;
  if (!binary) {
    PyObject *str = PyUnicode_FromStringAndSize(slice.data(), slice.size());
    if (str != nullptr) return str;
    PyErr_Clear();
  }
  return PyBytes_FromStringAndSize(slice.data(), slice.size());
}

void PyCursor::Define(PyObject *module) {
  InitType(&type, "sling.Cursor", sizeof(PyCursor), false);
  type.tp_dealloc = method_cast<destructor>(&PyCursor::Dealloc);
  type.tp_iter = method_cast<getiterfunc>(&PyCursor::Self);
  type.tp_iternext = method_cast<iternextfunc>(&PyCursor::Next);
  RegisterType(&type, module, "Cursor");
}

PyObject *PyCursor::Create(PyDatabase *pydb, Fields fields,
                      PyObject *args, PyObject *kw) {
  // Parse arguments.
  static const char *kwlist[] = {
    "begin", "end", "stable", "deletions", nullptr
  };

  uint64 begin = 0;
  uint64 end = -1;
  bool stable = false;
  bool deletions = false;
  if (args != nullptr || kw != nullptr) {
    bool ok = PyArg_ParseTupleAndKeywords(
                  args, kw, "|LLbb", const_cast<char **>(kwlist),
                  &begin, &end, &stable, &deletions);
    if (!ok) return nullptr;
  }

  // If a stable cursor is requested, only iterate to the current end of the
  // database, even if the database is modified during iteration.
  if (end == -1 && stable) {
    Status st = pydb->Transact([&]() -> Status {
      return pydb->db->Epoch(&end);
    });
    if (!CheckIO(st)) return nullptr;
  }

  // Return new cursor.
  PyCursor *cursor = PyObject_New(PyCursor, &type);
  cursor->Init(pydb, begin, end, fields, deletions);
  return cursor->AsObject();
}

void PyCursor::Init(PyDatabase *pydb, uint64 begin, uint64 end, Fields fields,
                    bool deletions) {
  this->pydb = pydb;
  this->fields = fields;
  Py_INCREF(pydb);

  // Initialize iterator.
  iterator = new DBIterator();
  iterator->position = begin;
  iterator->limit = end;
  iterator->batch = pydb->batchsize;
  iterator->deletions = deletions;
  iterator->buffer = new IOBuffer();
  if (fields == KEYS) iterator->novalue = true;
  next = 0;
  records = new std::vector<DBRecord>;
}

void PyCursor::Dealloc() {
  Py_DECREF(pydb);
  delete records;
  delete iterator->buffer;
  delete iterator;
  Free();
}

PyObject *PyCursor::Next() {
  // Fetch next batch of records if needed.
  if (next == records->size()) {
    records->clear();
    Status st = pydb->Transact([&]() -> Status {
      return pydb->db->Next(iterator, records);
    });
    if (!st.ok()) {
      if (st.code() == ENOENT) {
        PyErr_SetNone(PyExc_StopIteration);
      } else {
        PyErr_SetString(PyExc_IOError, st.message());
      }
      return nullptr;
    }
    next = 0;
    pydb->position = iterator->position;
  }

  // Return next record in batch.
  DBRecord &record = (*records)[next++];
  switch (fields) {
    case FULL: {
      PyObject *key = PyDatabase::PyValue(record.key, false);
      PyObject *version = PyLong_FromLong(record.version);
      PyObject *value = PyDatabase::PyValue(record.value);
      PyObject *triple = PyTuple_Pack(3, key, version, value);
      Py_DECREF(key);
      Py_DECREF(version);
      Py_DECREF(value);
      return triple;
    }

    case KEYS:
      return PyDatabase::PyValue(record.key, false);

    case VALUES:
      return PyDatabase::PyValue(record.value);

    case ITEMS: {
      PyObject *key = PyDatabase::PyValue(record.key, false);
      PyObject *value = PyDatabase::PyValue(record.value);
      PyObject *pair = PyTuple_Pack(2, key, value);
      Py_DECREF(key);
      Py_DECREF(value);
      return pair;
    }
  }

  return nullptr;
}

PyObject *PyCursor::Self() {
  Py_INCREF(this);
  return AsObject();
}

}  // namespace sling

