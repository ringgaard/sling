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

#include "sling/string/text.h"

#include <string.h>
#include <algorithm>
#include <string>

#include "sling/base/logging.h"
#include "sling/base/types.h"
#include "sling/string/ctype.h"

namespace sling {

const Text::size_type Text::npos = size_type(-1);

static const char *memmatch(const char *haystack, size_t hlen,
                            const char *needle, size_t nlen) {
  if (nlen == 0) return haystack;  // even if haylen is 0
  if (hlen < nlen) return nullptr;

  const char *match;
  const char *hayend = haystack + hlen - nlen + 1;
  while ((match = static_cast<const char *>(memchr(haystack, needle[0],
                                                   hayend - haystack)))) {
    if (memcmp(match, needle, nlen) == 0) return match;
    haystack = match + 1;
  }
  return nullptr;
}

static int memcasecmp(const char *s1, const char *s2, size_t n) {
  while (n-- > 0) {
    unsigned char u1 = ascii_toupper(*s1++);
    unsigned char u2 = ascii_toupper(*s2++);
    int diff = u1 - u2;
    if (diff) return diff;
  }
  return 0;
}

Text::Text(Text other, size_t pos)
    : Slice(other.data_ + pos, other.size_ - pos) {
  DCHECK_LE(0, pos);
  DCHECK_LE(pos, other.size_);
}

Text::Text(Text other, size_t pos, size_t len)
    : Slice(other.data_ + pos, std::min(len, other.size_ - pos)) {
  DCHECK_LE(0, pos);
  DCHECK_LE(pos, other.size_);
  DCHECK_GE(len, 0);
}

int Text::compare(Text t) const {
  const ssize_t min_size = size_ < t.size_ ? size_ : t.size_;
  int r = memcmp(data_, t.data_, min_size);
  if (r < 0) return -1;
  if (r > 0) return 1;
  if (size_ < t.size_) return -1;
  if (size_ > t.size_) return 1;
  return 0;
}

int Text::casecompare(Text t) const {
  const ssize_t min_size = size_ < t.size_ ? size_ : t.size_;
  int r = memcasecmp(data_, t.data_, min_size);
  if (r < 0) return -1;
  if (r > 0) return 1;
  if (size_ < t.size_) return -1;
  if (size_ > t.size_) return 1;
  return 0;
}

void Text::CopyToString(string *target) const {
  target->assign(data_, size_);
}

void Text::AppendToString(string *target) const {
  target->append(data_, size_);
}

ssize_t Text::copy(char *buf, size_type n, size_type pos) const {
  ssize_t ret = std::min(size_ - pos, n);
  memcpy(buf, data_ + pos, ret);
  return ret;
}

bool Text::contains(Text t) const {
  return find(t, 0) != npos;
}

ssize_t Text::find(Text t, size_type pos) const {
  if (size_ <= 0 || pos > static_cast<size_type>(size_)) {
    if (size_ == 0 && pos == 0 && t.size_ == 0) return 0;
    return npos;
  }
  const char *result = memmatch(data_ + pos, size_ - pos, t.data_, t.size_);
  return result ? result - data_ : npos;
}

ssize_t Text::find(char c, size_type pos) const {
  if (size_ <= 0 || pos >= static_cast<size_type>(size_)) {
    return npos;
  }
  const char *result =
    static_cast<const char*>(memchr(data_ + pos, c, size_ - pos));
  return result != nullptr ? result - data_ : npos;
}

ssize_t Text::rfind(Text t, size_type pos) const {
  if (size_ < t.size_) return npos;
  const size_t ulen = size_;
  if (t.size_ == 0) return std::min(ulen, pos);

  const char *last = data_ + std::min(ulen - t.size_, pos) + t.size_;
  const char *result = std::find_end(data_, last, t.data_, t.data_ + t.size_);
  return result != last ? result - data_ : npos;
}

// Search range is [0..pos] inclusive.  If pos == npos, search everything.
ssize_t Text::rfind(char c, size_type pos) const {
  if (size_ <= 0) return npos;
  ssize_t end = std::min(pos, static_cast<size_type>(size_ - 1));
  for (ssize_t i = end; i >= 0; --i) {
    if (data_[i] == c) return i;
  }
  return npos;
}

// For each character in characters_wanted, sets the index corresponding
// to the ASCII code of that character to 1 in table.  This is used by
// the find_.*_of methods below to tell whether or not a character is in
// the lookup table in constant time.
// The argument `table' must be an array that is large enough to hold all
// the possible values of an unsigned char.  Thus it should be be declared
// as follows:
//   bool table[UCHAR_MAX + 1]
static inline void BuildLookupTable(Text characters, bool *table) {
  const ssize_t length = characters.length();
  const char * const data = characters.data();
  for (ssize_t i = 0; i < length; ++i) {
    table[static_cast<unsigned char>(data[i])] = true;
  }
}

ssize_t Text::find_first_of(Text t, size_type pos) const {
  if (size_ <= 0 || t.size_ <= 0) return npos;

  // Avoid the cost of BuildLookupTable() for a single-character search.
  if (t.size_ == 1) return find_first_of(t.data_[0], pos);

  bool lookup[UCHAR_MAX + 1] = { false };
  BuildLookupTable(t, lookup);
  for (ssize_t i = pos; i < size_; ++i) {
    if (lookup[static_cast<unsigned char>(data_[i])]) {
      return i;
    }
  }
  return npos;
}

ssize_t Text::find_first_not_of(Text t, size_type pos) const {
  if (size_ <= 0) return npos;
  if (t.size_ <= 0) return 0;

  // Avoid the cost of BuildLookupTable() for a single-character search.
  if (t.size_ == 1) return find_first_not_of(t.data_[0], pos);

  bool lookup[UCHAR_MAX + 1] = { false };
  BuildLookupTable(t, lookup);
  for (ssize_t i = pos; i < size_; ++i) {
    if (!lookup[static_cast<unsigned char>(data_[i])]) {
      return i;
    }
  }
  return npos;
}

ssize_t Text::find_first_not_of(char c, size_type pos) const {
  if (size_ <= 0) return npos;

  for (; pos < static_cast<size_type>(size_); ++pos) {
    if (data_[pos] != c) return pos;
  }
  return npos;
}

ssize_t Text::find_last_of(Text t, size_type pos) const {
  if (size_ <= 0 || t.size_ <= 0) return npos;

  // Avoid the cost of BuildLookupTable() for a single-character search.
  if (t.size_ == 1) return find_last_of(t.data_[0], pos);

  bool lookup[UCHAR_MAX + 1] = { false };
  BuildLookupTable(t, lookup);
  ssize_t end = std::min(pos, static_cast<size_type>(size_ - 1));
  for (ssize_t i = end; i >= 0; --i) {
    if (lookup[static_cast<unsigned char>(data_[i])]) {
      return i;
    }
  }
  return npos;
}

ssize_t Text::find_last_not_of(Text t, size_type pos) const {
  if (size_ <= 0) return npos;

  ssize_t i = std::min(pos, static_cast<size_type>(size_ - 1));
  if (t.size_ <= 0) return i;

  // Avoid the cost of BuildLookupTable() for a single-character search.
  if (t.size_ == 1) return find_last_not_of(t.data_[0], pos);

  bool lookup[UCHAR_MAX + 1] = { false };
  BuildLookupTable(t, lookup);
  for (; i >= 0; --i) {
    if (!lookup[static_cast<unsigned char>(data_[i])]) {
      return i;
    }
  }
  return npos;
}

ssize_t Text::find_last_not_of(char c, size_type pos) const {
  if (size_ <= 0) return npos;

  ssize_t end = std::min(pos, static_cast<size_type>(size_ - 1));
  for (ssize_t i = end; i >= 0; --i) {
    if (data_[i] != c) return i;
  }
  return npos;
}

Text Text::substr(size_type pos, size_type n) const {
  if (pos > size_) pos = size_;
  if (n > size_ - pos) n = size_ - pos;
  return Text(data_ + pos, n);
}

std::vector<Text> Text::split(char c) const {
  std::vector<Text> parts;
  ssize_t start = 0;
  while (start < size_) {
    ssize_t end = find(c, start);
    if (end == npos) end = size_;
    parts.emplace_back(data_ + start, end - start);
    start = end + 1;
  }
  return parts;
}

Text Text::trim() const {
  const char *ptr = data_;
  const char *end = data_ + size_;
  while (ptr < end && ascii_isspace(*ptr)) ptr++;
  while (end > ptr && ascii_isspace(end[-1])) end--;
  return Text(ptr, end - ptr);
}

std::ostream &operator <<(std::ostream &o, Text t) {
  o.write(t.data(), t.size());
  return o;
}

}  // namespace sling
