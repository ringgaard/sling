// Copyright 2017 Google Inc.
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

// Derived from Google StringPiece.

#ifndef SLING_STRING_TEXT_H_
#define SLING_STRING_TEXT_H_

#include <string.h>
#include <iosfwd>
#include <limits>
#include <string>
#include <vector>

#include "sling/base/logging.h"
#include "sling/base/port.h"
#include "sling/base/slice.h"
#include "sling/base/types.h"
#include "sling/util/city.h"

namespace sling {

class Text : public Slice {
 public:
  // Constructors.
  Text() : Slice() {}
  Text(const Slice &slice) : Slice(slice) {}
  Text(const char *str) : Slice(str, str ? strlen(str) : 0) {}
  Text(const string &str) : Slice(str.data(), str.size()) {}
  Text(const char *str, size_t len) : Slice(str, len) {}

  // Substring of another text.
  // pos must be non-negative and <= x.length().
  Text(Text other, size_t pos);

  // Substring of another text.
  // pos must be non-negative and <= x.length().
  // len must be non-negative and will be pinned to at most x.length() - pos.
  Text(Text other, size_t pos, size_t len);

  // Access to string buffer.
  size_t length() const { return size_; }

  // Set contents.
  void set(const char *data, size_t len) {
    DCHECK_GE(len, 0);
    data_ = data;
    size_ = len;
  }

  void set(const char *str) {
    data_ = str;
    size_ = str ? strlen(str) : 0;
  }

  void set(const void *data, size_t len) {
    data_ = reinterpret_cast<const char *>(data);
    size_ = len;
  }

  // Compare text to another text. Returns {-1, 0, 1}
  int compare(Text t) const;

  // Compare text to another text ignoring the case of the characters.
  int casecompare(Text t) const;

  string as_string() const { return str(); }
  string ToString() const { return str(); }

  // Copy text to string.
  void CopyToString(string *target) const;

  // Append text to string.
  void AppendToString(string *target) const;

  // STL container definitions.
  typedef char value_type;
  typedef const char *pointer;
  typedef const char &reference;
  typedef const char &const_reference;
  typedef size_t size_type;
  typedef ptrdiff_t difference_type;
  static const size_type npos;

  // Iterators.
  typedef const char *const_iterator;
  typedef const char *iterator;
  typedef std::reverse_iterator<const_iterator> const_reverse_iterator;
  typedef std::reverse_iterator<iterator> reverse_iterator;

  iterator begin() const { return data_; }
  iterator end() const { return data_ + size_; }
  const_reverse_iterator rbegin() const {
    return const_reverse_iterator(data_ + size_);
  }
  const_reverse_iterator rend() const {
    return const_reverse_iterator(data_);
  }
  ssize_t max_size() const { return size_; }
  ssize_t capacity() const { return size_; }
  ssize_t copy(char *buf, size_type n, size_type pos = 0) const;

  // Checks if text contains another text.
  bool contains(Text t) const;

  // Find operations.
  ssize_t find(Text t, size_type pos = 0) const;
  ssize_t find(char c, size_type pos = 0) const;
  ssize_t rfind(Text t, size_type pos = npos) const;
  ssize_t rfind(char c, size_type pos = npos) const;

  ssize_t find_first_of(Text t, size_type pos = 0) const;
  ssize_t find_first_of(char c, size_type pos = 0) const {
    return find(c, pos);
  }
  ssize_t find_first_not_of(Text t, size_type pos = 0) const;
  ssize_t find_first_not_of(char c, size_type pos = 0) const;
  ssize_t find_last_of(Text t, size_type pos = npos) const;
  ssize_t find_last_of(char c, size_type pos = npos) const {
    return rfind(c, pos);
  }
  ssize_t find_last_not_of(Text t, size_type pos = npos) const;
  ssize_t find_last_not_of(char c, size_type pos = npos) const;

  // Substring.
  Text substr(size_type pos, size_type n = npos) const;

  // Split text on delimiter character.
  std::vector<Text> split(char c) const;

  // Trim whitespace from begining and end of text.
  Text trim() const;
};

// Comparison operators.
inline bool operator ==(Text x, Text y) {
  return x.size() == y.size() && memcmp(x.data(), y.data(), x.size()) == 0;
}

inline bool operator !=(Text x, Text y) {
  return !(x == y);
}

inline bool operator <(Text x, Text y) {
  const ssize_t min_size = x.size() < y.size() ? x.size() : y.size();
  const int r = memcmp(x.data(), y.data(), min_size);
  return (r < 0) || (r == 0 && x.size() < y.size());
}

inline bool operator >(Text x, Text y) {
  return y < x;
}

inline bool operator <=(Text x, Text y) {
  return !(x > y);
}

inline bool operator >=(Text x, Text y) {
  return !(x < y);
}

// Allow text to be streamed.
extern std::ostream &operator <<(std::ostream &o, Text t);

}  // namespace sling

namespace std {

template<> struct hash<sling::Text> {
  size_t operator()(sling::Text t) const {
    return sling::CityHash64(t.data(), t.size());
  }
};

}  // namespace std

#endif  // STRING_TEXT_H_
