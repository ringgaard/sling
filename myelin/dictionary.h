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

#ifndef MYELIN_DICTIONARY_H_
#define MYELIN_DICTIONARY_H_

#include "base/types.h"
#include "myelin/flow.h"
#include "third_party/jit/code.h"

namespace sling {
namespace myelin {

struct DictionaryItem {
  uint64 hash;
  int64 value;
};

typedef DictionaryItem *DictionaryBucket;

class Dictionary {
 public:
  ~Dictionary();

  // Initialize dictionary from lexicon blob.
  void Init(Flow::Blob *lexicon);

  // Lookup word in dictionary.
  int64 Lookup(const string &word) const {
    return lookup_.Execute(word.data(), word.size());
  }
  int64 LookupSlow(const string &word) const;

 private:
  DictionaryBucket *buckets_ = nullptr;
  DictionaryItem *items_ = nullptr;
  int num_buckets_ = 0;
  int size_ = 0;
  int64 oov_ = -1;
  jit::Code lookup_;
};

}  // namespace myelin
}  // namespace sling

#endif  // MYELIN_DICTIONARY_H_

