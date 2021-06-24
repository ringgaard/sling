// Copyright 2021 Ringgaard Research ApS
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

#ifndef SLING_UTIL_JSON_H_
#define SLING_UTIL_JSON_H_

#include <string>
#include <vector>
#include <utility>

#include "sling/base/types.h"
#include "sling/util/iobuffer.h"

namespace sling {

// Simple JSON data structure.
class JSON {
 public:
  // Forward declarations.
  class Object;
  class Array;

  // JSON value types.
  enum Type {NIL, INT, FLOAT, BOOL, STRING, OBJECT, ARRAY};

  // JSON object with set of key/value pairs.
  class Object {
   public:
    // Add key/value pair to object. Takes ownership of arrays and objects.
    void Add(const string &key, int64 value);
    void Add(const string &key, double value);
    void Add(const string &key, bool value);
    void Add(const string &key, const string &value);
    void Add(const string &key, Object *value);
    void Add(const string &key, Array *value);

    // Add new object with key to object.
    Object *AddObject(const string &key);

    // Add new array with key to object.
    Array *AddArray(const string &key);

    // Write object as JSON to output.
    void Write(IOBuffer *buffer) const;

   private:
    // Key/value pairs for object.
    std::vector<std::pair<string, JSON>> items_;
  };

  // JSON array with list of values.
  class Array {
   public:
    // Add value to array. Takes ownership of arrays and objects.
    void Add(int64 value);
    void Add(double value);
    void Add(bool value);
    void Add(const string &value);
    void Add(Object *value);
    void Add(Array *value);

    // Add new object element to array.
    Object *AddObject(const string &key);

    // Add new array element to array.
    Array *AddArray(const string &key);

    // Write array as JSON to output.
    void Write(IOBuffer *buffer) const;

   private:
    // Array elements.
    std::vector<JSON> elements_;
  };

  // Default constructor.
  JSON() : type_(NIL), i_(0) {}

  // Move constructor.
  JSON(JSON &&other) : type_(other.type_), i_(other.i_) { other.type_ = NIL; }

  // Initialize JSON value. Takes ownership of arrays and objects.
  JSON(int64 value) : type_(INT), i_(value) {}
  JSON(double value) : type_(FLOAT), f_(value) {}
  JSON(bool value) : type_(BOOL), b_(value) {}
  JSON(const string &value) : type_(STRING), s_(new string(value)) {}
  JSON(Object *value) : type_(OBJECT), o_(value) {}
  JSON(Array *value) : type_(ARRAY), a_(value) {}

  // Delete JSON value.
  ~JSON();

  // Write value in JSON format to output buffer.
  void Write(IOBuffer *output) const;

 private:
  // JSON type.
  Type type_;

  // JSON value.
  union {
   uint64 i_;   // INTEGER
   double f_;   // FLOAT
   bool b_;     // BOOL
   string *s_;  // STRING
   Object *o_;  // OBJECT
   Array *a_;   // ARRAY
  };

  JSON& operator=(const JSON &) = delete;
  JSON(const JSON &) = delete;
};

}  // namespace sling

#endif  // SLING_UTIL_JSON_H_

