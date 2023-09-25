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

#include "sling/nlp/document/subword-tokenizer.h"

#include "sling/util/unicode.h"

namespace sling {
namespace nlp {

void SubwordTokenizer::Init(Vocabulary::Iterator *vocabulary) {
  // Get leading and trailing subwords from vocabulary.
  Vocabulary::TextVectorMap leading;
  Vocabulary::TextVectorMap trailing;
  Text word;
  vocabulary->Reset();
  int index = 0;
  while (vocabulary->Next(&word, nullptr)) {
    subwords_.push_back(word.str());
    word = subwords_.back();
    if (word.size() >= 2 && word[0] == '#' && word[1] == '#') {
      trailing.emplace_back(word.substr(2), index++);
    } else {
      leading.emplace_back(word, index++);
    }
  }

  // Initialize leading and trailing lexicons.
  Vocabulary::TextVectorMapIterator l(leading);
  leading_.Init(&l, true);
  Vocabulary::TextVectorMapIterator t(trailing);
  trailing_.Init(&t, true);
  oov_ = leading_.Lookup("[UNK]");
}

void SubwordTokenizer::Write(string *buffer, char terminator) const {
  for (const string &subword : subwords_) {
    buffer->append(subword);
    buffer->push_back(terminator);
  }
}

int SubwordTokenizer::Tokenize(Text word, std::vector<int> *subwords) const {
  // Fast path is checking if the whole word is a leading token.
  int index = leading_.Lookup(word);
  if (index != -1) {
    subwords->push_back(index);
    return 1;
  }

  // Break word into subwords by incrementally matching maximal prefixes of
  // the remaining suffix.
  const char *p = word.data();
  const char *end = p + word.size();
  int num_subwords = 0;
  while (p < end) {
    const char *q = end;
    while (q > p) {
      int index = Lookup(Text(p, q - p), num_subwords == 0);
      if (index != oov_) {
        subwords->push_back(index);
        break;
      }
      q = UTF8::Previous(q, p);
    }
    if (q > p) {
      // Match remainder.
      p = q;
      num_subwords++;
    } else {
      // Out of vocabulary.
      if (num_subwords == 0) subwords->push_back(oov_);
      return -1;
    }
  }

  return num_subwords;
}

string SubwordTokenizer::TokenizedWord(Text word) const {
  std::vector<int> subwords;
  Tokenize(word, &subwords);
  string str;
  for (int index : subwords) {
    str.append(subwords_[index]);
  }
  return str;
}

}  // namespace nlp
}  // namespace sling

