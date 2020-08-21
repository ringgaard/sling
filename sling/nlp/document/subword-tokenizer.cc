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

const int SubwordTokenizer::OOV;

void SubwordTokenizer::Init(Vocabulary::Iterator *leading,
                            Vocabulary::Iterator *trailing) {
  Text subword;
  int64 index;

  // Get leading subwords.
  leading_subwords_.resize(leading->Size());
  leading->Reset();
  index = 0;
  while (leading->Next(&subword, nullptr)) {
    leading_subwords_[index++] = subword.str();
  }

  // Get trailing subwords.
  trailing_subwords_.resize(trailing->Size());
  trailing->Reset();
  index = 0;
  while (trailing->Next(&subword, nullptr)) {
    trailing_subwords_[index++] = subword.str();
  }

  // Initialize leading and trailing lexicons.
  leading_.Init(leading);
  trailing_.Init(trailing);
}

void SubwordTokenizer::Init(Vocabulary::Iterator *vocabulary) {
  // Get leading and trailing subwords from vocabulary.
  Text word;
  vocabulary->Reset();
  while (vocabulary->Next(&word, nullptr)) {
    CHECK_GE(word.size(), 2);
    Text subword(word, 1);
    if (word[0] == '_') {
      leading_subwords_.push_back(subword.str());
    } else if (word[0] == '#') {
      trailing_subwords_.push_back(subword.str());
    }
  }

  // Initialize leading and trailing lexicons.
  Vocabulary::VectorIterator l(leading_subwords_);
  leading_.Init(&l);
  Vocabulary::VectorIterator t(trailing_subwords_);
  trailing_.Init(&t);
}

void SubwordTokenizer::WriteLeading(string *buffer, char terminator) const {
  for (const string &subword : leading_subwords_) {
    buffer->append(subword);
    buffer->push_back(terminator);
  }
}

void SubwordTokenizer::WriteTrailing(string *buffer, char terminator) const {
  for (const string &subword : trailing_subwords_) {
    buffer->append(subword);
    buffer->push_back(terminator);
  }
}

int SubwordTokenizer::Lookup(Text subword, bool leading) const {
  if (leading) {
    int index = leading_.Lookup(subword);
    if (index == -1) return OOV;
    return index + 1;
  } else {
    int index = trailing_.Lookup(subword);
    if (index == -1) return OOV;
    return index + leading_.size() + 1;
  }
}

int SubwordTokenizer::Tokenize(Text word, std::vector<int> *subwords) const {
  // Fast path is checking if the whole word is a leading token.
  int index = leading_.Lookup(word);
  if (index != -1) {
    subwords->push_back(index + 1);
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
      if (index != OOV) {
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
      if (num_subwords == 0) subwords->push_back(OOV);
      return -1;
    }
  }

  return num_subwords;
}

string SubwordTokenizer::TokenizedWord(Text word) const {
  std::vector<int> subwords;
  Tokenize(word, &subwords);
  string str;
  bool first = true;
  for (int index : subwords) {
    if (!first) str.append("##");
    first = false;
    str.append(Subword(index));
  }
  return str;
}

const string &SubwordTokenizer::Subword(int index) const {
  static string unknown("<UNK>");
  if (index == 0) {
    return unknown;
  } else if (index < leading_subwords_.size() + 1) {
    return leading_subwords_[index - 1];
  } else {
    return trailing_subwords_[index - leading_subwords_.size() - 1];
  }
}

}  // namespace nlp
}  // namespace sling

