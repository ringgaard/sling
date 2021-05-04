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

#ifndef SLING_PYAPI_PYDATABASE_H_
#define SLING_PYAPI_PYDATABASE_H_

#include "sling/db/dbclient.h"
#include "sling/pyapi/pybase.h"

namespace sling {

// Python wrapper for database client.
struct PyDatabase : public PyBase {
  // Initialize record reader wrapper.
  int Init(PyObject *args, PyObject *kwds);

  // Deallocate database client wrapper.
  void Dealloc();

  // Close database connection.
  PyObject *Close();

  // Get record.
  PyObject *Get(PyObject *obj);

  // Put record. Return outcome.
  PyObject *Put(PyObject *args, PyObject *kw);

  // Add record. Return outcome.
  PyObject *Add(PyObject *args, PyObject *kw);

  // Delete record.
  PyObject *Delete(PyObject *key);

  // Fetch record for key.
  PyObject *Lookup(PyObject *key);

  // Update/add record for key
  int Assign(PyObject *key, PyObject *v);

  // Return iterator for records.
  PyObject *Iterator();
  PyObject *Keys();
  PyObject *Values();
  PyObject *Items();

  // Return iterator starting at position.
  PyObject *Start(PyObject *args, PyObject *kw);

  // Return current position in database.
  PyObject *Position();

  // Get slice for string or binary value.
  static bool GetData(PyObject *obj, Slice *data);

  // Get data as Python object.
  static PyObject *PyValue(const Slice &slice, bool binary = true);

  // Database client.
  DBClient *db;

  // Batch size.
  int batchsize;

  // Current postion in database.
  int position;

  // Registration.
  static PyTypeObject type;
  static PyMethodTable methods;
  static PyMappingMethods mapping;
  static void Define(PyObject *module);
};

// Python wrapper for database cursor.
struct PyCursor : public PyBase {
  // Fields returned from cursor.
  enum Fields {
    FULL,    // key, version, value
    KEYS,    // only keys
    VALUES,  // only values
    ITEMS,   // key, value
  };

  // Initialize database iterator.
  void Init(PyDatabase *pydb, uint64 start, Fields fields);

  // Deallocate database cursor.
  void Dealloc();

  // Return next record.
  PyObject *Next();

  // Return self.
  PyObject *Self();

  // Database connection.
  PyDatabase *pydb;

  // Fields to return for cursor iterator.
  Fields fields;

  // Current position in database.
  uint64 iterator;

  // Next record in batch.
  int next;

  // Current record batch.
  std::vector<DBRecord> *records;

  // I/O Buffer for fetching records.
  IOBuffer *buffer;

  // Registration.
  static PyTypeObject type;
  static void Define(PyObject *module);
};

}  // namespace sling

#endif  // SLING_PYAPI_PYDATABASE_H_

