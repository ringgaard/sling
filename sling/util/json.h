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
#include "sling/string/text.h"
#include "sling/util/iobuffer.h"

namespace sling {

// Simple JSON data structure.
class JSON {
 public:
  // Forward declarations.
  class Object;
  class Array;

  // JSON value types.
  enum Type {NIL, INT, FLOAT, BOOL, STRING, OBJECT, ARRAY, ERROR};

  // JSON object with set of key/value pairs.
  class Object {
   public:
    // Add key/value pair to object. Takes ownership of arrays and objects.
    void Add(const string &key, int64 value) {
      items_.emplace_back(key, JSON(value));
    }
    void Add(const string &key, uint64 value) {
      items_.emplace_back(key, JSON(value));
    }
    void Add(const string &key, int value) {
      items_.emplace_back(key, JSON(value));
    }
    void Add(const string &key, double value) {
      items_.emplace_back(key, JSON(value));
    }
    void Add(const string &key, bool value) {
      items_.emplace_back(key, JSON(value));
    }
    void Add(const string &key, const string &value) {
      items_.emplace_back(key, JSON(value));
    }
    void Add(const string &key, const char *value) {
      items_.emplace_back(key, JSON(value));
    }
    void Add(const string &key, Text value) {
      items_.emplace_back(key, JSON(value));
    }
    void Add(const string &key, Object *value) {
      items_.emplace_back(key, JSON(value));
    }
    void Add(const string &key, Array *value) {
      items_.emplace_back(key, JSON(value));
    }
    void Add(const string &key, JSON &value) {
      items_.emplace_back(key, std::move(value));
    }

    // Add new object with key to object.
    Object *AddObject(const string &key);

    // Add new array with key to object.
    Array *AddArray(const string &key);

    // Write object as JSON to output.
    void Write(IOBuffer *buffer) const;

    // Return object in JSON format.
    std::string AsString() const;

    // Get object size.
    int size() const { return items_.size(); }

    // Get keys and values.
    const string &key(int index) const { return items_[index].first; }
    const JSON &value(int index) const { return items_[index].second; }

    // Look up value.
    const JSON &operator [](const string &key) const;
    const JSON &operator [](const char *key) const;

   private:
    // Key/value pairs for object.
    std::vector<std::pair<string, JSON>> items_;
  };

  // JSON array with list of values.
  class Array {
   public:
    // Add value to array. Takes ownership of arrays and objects.
    void Add(int64 value) {
      elements_.emplace_back(JSON(value));
    }
    void Add(uint64 value) {
      elements_.emplace_back(JSON(value));
    }
    void Add(int value) {
      elements_.emplace_back(JSON(value));
    }
    void Add(double value) {
      elements_.emplace_back(JSON(value));
    }
    void Add(bool value) {
      elements_.emplace_back(JSON(value));
    }
    void Add(const string &value) {
      elements_.emplace_back(JSON(value));
    }
    void Add(const char *value) {
      elements_.emplace_back(JSON(value));
    }
    void Add(Text value) {
      elements_.emplace_back(JSON(value));
    }
    void Add(Object *value) {
      elements_.emplace_back(JSON(value));
    }
    void Add(Array *value) {
      elements_.emplace_back(JSON(value));
    }
    void Add(JSON &value) {
      elements_.emplace_back(std::move(value));
    }

    // Add new object element to array.
    Object *AddObject();

    // Add new array element to array.
    Array *AddArray();

    // Write array as JSON to output.
    void Write(IOBuffer *buffer) const;

    // Get array size.
    int size() const { return elements_.size(); }

    // Get element value.
    const JSON &operator [](int i) const { return elements_[i]; }

   private:
    // Array elements.
    std::vector<JSON> elements_;
  };

  // JSON parser.
  class Parser {
   public:
    Parser(IOBuffer *input) : input_(input) { next(); }

    // Read next character from input.
    void next() {
      current_ = input_->empty() ? -1 : *input_->consume<unsigned char>(1);
      if (current_ == '\n') line_++;
    }

    // Parse JSON element from input.
    JSON Parse();

    // Current line number.
    int line() const { return line_; }
   private:
    // Skip whitespace.
    void SkipWhitespace();

    // Parse object.
    JSON ParseObject();

    // Parse array.
    JSON ParseArray();

    // Parse string.
    bool ParseString();

    // Parse number.
    JSON ParseNumber();

    // Parse 4-digit hex escape.
    int ParseHex();

    // Input buffer.
    IOBuffer *input_;

    // Current input character or -1 if end of input has been reached.
    int current_;

    // Current line number.
    int line_ = 1;

    // Buffer for current token.
    string token_;

  };

  // Default constructor.
  JSON() : i_(0), type_(ERROR) {}

  // Move constructor.
  JSON(JSON &&other) : i_(other.i_), type_(other.type_)  {
    other.type_ = ERROR;
  }

  // Initialize JSON value. Takes ownership of arrays and objects.
  JSON(Type t) : i_(0), type_(t) {}
  JSON(int64 value) : i_(value), type_(INT) {}
  JSON(uint64 value) : i_(value), type_(INT) {}
  JSON(int value) : i_(value), type_(INT) {}
  JSON(double value) : f_(value), type_(FLOAT) {}
  JSON(bool value) : i_(value), type_(BOOL) {}
  JSON(const string &value) : s_(new string(value)), type_(STRING) {}
  JSON(const char *value) : s_(new string(value)), type_(STRING)  {}
  JSON(Text value) : s_(new string(value.str())), type_(STRING) {}
  JSON(Object *value) : o_(value), type_(OBJECT) {}
  JSON(Array *value) : a_(value), type_(ARRAY) {}

  // Delete JSON value.
  ~JSON();

  // Write value in JSON format to output buffer.
  void Write(IOBuffer *output) const;

  // Return value in JSON format.
  string AsString() const;

  // Read value in JSON format from input buffer.
  static JSON Read(IOBuffer *input);
  static JSON Read(const string &json);

  // Get JSON value type.
  Type type() const { return type_; }

  // Get JSON value.
  int64 i(int64 defval = 0) const { return type_ == INT ? i_ : defval; }
  bool b(bool defval = false) const { return type_ == BOOL ? i_ : defval; }
  double f(double defval = 0.0) const { return type_ == FLOAT ? f_ : defval; }
  const string &s() const { return type_ == STRING ? *s_ : EMPTY_STRING; }
  const char *c() const { return type_ == STRING ? s_->c_str() : nullptr; }
  Text t() const { return type_ == STRING ? Text(*s_) : Text(); }
  Object *o() const { return type_ == OBJECT ? o_ : nullptr; }
  Array *a() const { return type_ == ARRAY ? a_ : nullptr; }

  // Object/array lookup.
  const JSON &operator [](const string &key) const {
    return type_ == OBJECT ? (*o_)[key] : ERROR_VALUE;
  }
  const JSON &operator [](const char *key) const {
    return type_ == OBJECT ? (*o_)[key] : ERROR_VALUE;
  }
  const JSON &operator [](int i) const {
    return type_ == ARRAY ? (*a_)[i] : ERROR_VALUE;
  }

  // Cast operators.
  operator int() const { return i(); }
  operator int64() const { return i(); }
  operator bool() const { return b(); }
  operator double() const { return f(); }
  operator float() const { return f(); }
  operator const string &() const { return s(); }
  operator Text() const { return t(); }
  operator Object &() const { return *o(); }
  operator const Object &() const { return *o(); }
  operator Array &() const { return *a(); }
  operator const Array &() const { return *a(); }

  // Check for errors.
  bool valid() const { return type_ != ERROR; }

  // Take ownership of other JSON value.
  void MoveFrom(JSON &other) {
    CHECK_EQ(type_, ERROR);
    i_ = other.i_;
    type_ = other.type_;
    other.type_ = ERROR;
  }

 private:
  // JSON value.
  union {
   int64 i_;        // INTEGER/BOOL
   double f_;       // FLOAT
   string *s_;      // STRING
   Object *o_;      // OBJECT
   Array *a_;       // ARRAY
  };

  static string EMPTY_STRING;
  static JSON ERROR_VALUE;

  // JSON type.
  Type type_;

  // No copy or assign.
  JSON &operator=(const JSON &) = delete;
  JSON(const JSON &) = delete;
};

}  // namespace sling

#endif  // SLING_UTIL_JSON_H_
