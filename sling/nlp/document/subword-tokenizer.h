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
  // Initialize tokenizer lexicon from vocabulary where trailing subwords begin
  // with ##.
  void Init(Vocabulary::Iterator *vocabulary);

  // Write subwords to buffer.
  void Write(string *buffer, char terminator = 0) const;

  // Break word into subword tokens. Returns the number of subwords or -1 if the
  // word could not be broken into subwords using the vocabulary. In this case
  // an OOV is added to the subwords vector.
  int Tokenize(Text word, std::vector<int> *subwords) const;

  // Return word with subword markers (##).
  string TokenizedWord(Text word) const;

  // Look up trailing/leading subword in lexicon and return index or -1 if
  // the subword is not in the vocabulary.
  int Lookup(Text subword, bool leading) const {
    return leading ? leading_.Lookup(subword) : trailing_.Lookup(subword);
  }

  // Lexicon size.
  int size() const { return subwords_.size(); }

  // Return subword from id.
  const string &subword(int index) const { return subwords_[index]; }

 private:
  // Leading and trailing subword token lexicons.
  Vocabulary leading_;
  Vocabulary trailing_;

  // Subwords. Trailing subwords starts with ##.
  std::vector<string> subwords_;

  // The out-of-vocabulary (OOV) token index.
  int oov_ = 0;
};

}  // namespace nlp
}  // namespace sling

#endif  // SLING_NLP_DOCUMENT_SUBWORD_TOKENIZER_H_

