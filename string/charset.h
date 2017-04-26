// Copyright 2013 Google Inc. All Rights Reserved.
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

#ifndef STRING_CHARSET_H_
#define STRING_CHARSET_H_

#include "base/types.h"
#include "string/text.h"

namespace sling {

// A CharSet is a simple map from (1-byte) characters to Booleans. It simply
// exposes the mechanism of checking if a given character is in the set, fairly
// efficiently. Useful for string tokenizing routines.
class CharSet {
 public:
  // Initialize a CharSet containing no characters or the given set of
  // characters, respectively.
  CharSet();

  // Deliberately an implicit constructor, so anything that takes a CharSet
  // can also take an explicit list of characters.
  CharSet(const char *characters);
  explicit CharSet(Text characters);

  // Add or remove a character from the set.
  void Add(unsigned char c) { bits_[Word(c)] |= BitMask(c); }
  void Remove(unsigned char c) { bits_[Word(c)] &= ~BitMask(c); }

  // Return true if this character is in the set
  bool Test(unsigned char c) const { return bits_[Word(c)] & BitMask(c); }

 private:
  // Bits representing characters in CharSet.
  uint64 bits_[4];

  // 4 words => the high 2 bits of c are the word number. In general,
  // kShiftValue = 8 - log2(kNumWords)
  static int Word(unsigned char c) { return c >> 6; }

  // And the value we AND with c is ((1 << shift value) - 1)
  // static const int kLowBitsMask = (256 / kNumWords) - 1;
  static uint64 BitMask(unsigned char c) {
    uint64 mask = 1;
    return mask << (c & 0x3f);
  }
};

}  // namespace sling

#endif  // STRING_CHARSET_H_

