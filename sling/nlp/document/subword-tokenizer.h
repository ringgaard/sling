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

#ifndef SLING_NLP_DOCUMENT_SUBWORD_TOKENIZER_H_
#define SLING_NLP_DOCUMENT_SUBWORD_TOKENIZER_H_

#include "sling/base/types.h"
#include "sling/util/vocabulary.h"

namespace sling {
namespace nlp {

// Subword tokenizer for splitting tokens into leading and trailing subword
// tokens.
class SubwordTokenizer {
 public:
  // The out-of-vocabulary (OOV) token index.
  static const int OOV = 0;

  // Initialize tokenizer lexicon from leading and trailing subword
  // vocabularies.
  void Init(Vocabulary::Iterator *leading, Vocabulary::Iterator *trailing);

  // Initialize tokenizer lexicon from vocabulary where leading subwords are
  // begin with _ and trailing subwords begin with #.
  void Init(Vocabulary::Iterator *vocabulary);

  // Write leading/trailing subwords to buffer.
  void WriteLeading(string *buffer, char terminator = 0) const;
  void WriteTrailing(string *buffer, char terminator = 0) const;

  // Lexicon size including the OOV entry.
  int size() const { return leading_.size() + trailing_.size() + 1; }

  // Look up trailing/leading subword in lexicon and return index or OOV if
  // the subword is not in the vocabulary.
  int Lookup(Text subword, bool leading) const;

  // Break word into subword tokens. Returns the number of subwords or -1 if the
  // word could not be broken into subwords using the vocabulary. In this case
  // an OOV is added to the subwords vector.
  int Tokenize(Text word, std::vector<int> *subwords) const;

  // Return word with subword markers (##).
  string TokenizedWord(Text word) const;

  // Return subword for index.
  const string &Subword(int index) const;

 private:
  // Leading and trailing subword token lexicons.
  Vocabulary leading_;
  Vocabulary trailing_;

  // Subwords for leading and trailing subwords.
  std::vector<string> leading_subwords_;
  std::vector<string> trailing_subwords_;
};

}  // namespace nlp
}  // namespace sling

#endif  // SLING_NLP_DOCUMENT_SUBWORD_TOKENIZER_H_

