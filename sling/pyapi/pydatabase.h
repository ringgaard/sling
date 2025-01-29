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

#include <functional>

#include "sling/db/dbclient.h"
#include "sling/pyapi/pybase.h"
#include "sling/util/mutex.h"

namespace sling {

// Python wrapper for database client.
struct PyDatabase : public PyBase {
  // Database transaction.
  typedef std::function<Status()> Transaction;

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

  // Check if database has record for key.
  int Contains(PyObject *key);

  // Fetch record for key.
  PyObject *Lookup(PyObject *key);

  // Update/add record for key
  int Assign(PyObject *key, PyObject *v);

  // Return iterator for records.
  PyObject *Iterator();
  PyObject *Keys(PyObject *args, PyObject *kw);
  PyObject *Values(PyObject *args, PyObject *kw);
  PyObject *Items(PyObject *args, PyObject *kw);
  PyObject *Full(PyObject *args, PyObject *kw);

  // Return current position in database.
  PyObject *Position();

  // Return current epoch for database.
  PyObject *Epoch();

  // Clear all records from database.
  PyObject *Clear();

  // Get slice for string or binary value.
  static bool GetData(PyObject *obj, Slice *data);

  // Get data as Python object.
  static PyObject *PyValue(const Slice &slice, bool binary = true);

  // Perform database operation. This will release the Python GIL and ensure
  // exclusive access to the database connection.
  Status Transact(Transaction tx);

  // Database client.
  DBClient *db;

  // Batch size.
  int batchsize;

  // Current postion in database.
  uint64 position;

  // Mutex for serializing access to the database connection.
  Mutex *mu;

  // Registration.
  static PyTypeObject type;
  static PyMethodTable methods;
  static PyMappingMethods mapping;
  static PySequenceMethods sequence;
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

  // Create new database cursor.
  static PyObject *Create(PyDatabase *pydb, Fields fields,
                          PyObject *args, PyObject *kw);

  // Initialize database iterator.
  void Init(PyDatabase *pydb, uint64 begin, uint64 end, Fields fields,
            bool deletions);

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

  // Database iterator.
  DBIterator *iterator;

  // Next record in batch.
  int next;

  // Current record batch.
  std::vector<DBRecord> *records;

  // Registration.
  static PyTypeObject type;
  static void Define(PyObject *module);
};

}  // namespace sling

#endif  // SLING_PYAPI_PYDATABASE_H_
