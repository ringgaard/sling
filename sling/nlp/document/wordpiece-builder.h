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

#ifndef SLING_NLP_DOCUMENT_WORDPIECE_BUILDER_H_
#define SLING_NLP_DOCUMENT_WORDPIECE_BUILDER_H_

#include <functional>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "sling/base/types.h"
#include "sling/util/vocabulary.h"

namespace sling {
namespace nlp {

// Build wordpiece model from vocabulary using the byte-pair-encoding algorithm.
class WordPieceBuilder {
 public:
  // Symbol representing a character or a symbol bigram.
  struct Symbol {
    Symbol *left = nullptr;     // left symbol in bigram
    Symbol *right = nullptr;    // right symbol in bigram
    int code = 0;               // character code point for unigram symbol
    bool trailing = false;      // trailing symbol in word, i.e. not first
    bool selected = false;      // symbol has been selected for output
    int64 freq = 0;             // symbol frequency

    // Check if symbol is a bigram.
    bool bigram() const { return left != nullptr && right != nullptr; }

    // Get text for symbol.
    string text() const {
      string str;
      AppendToString(&str);
      return str;
    }

    // Append symbol text to string.
    void AppendToString(string *str) const;
  };

  // Callback for outputting word piece symbols.
  typedef std::function<void(const Symbol *symbol)> Emitter;

  WordPieceBuilder(int max_size) : max_size_(max_size) {}
  ~WordPieceBuilder();

  // Build wordpiece model from vocabulary and output word pieces to emitter.
  void Build(Vocabulary::Iterator *vocabulary, const Emitter &emit);

 private:
  // A word in the word vocabulary.
  struct Word {
    std::vector<Symbol *> symbols;   // word encoded as symbols
    int freq;                        // word frequency
  };

  // Symbol pair.
  typedef std::pair<Symbol *, Symbol *> Bigram;
  typedef std::hash<Symbol *> SymbolHash;
  struct BigramHash {
	  size_t operator()(const Bigram &bigram) const {
		  return SymbolHash()(bigram.first) ^ SymbolHash()(bigram.second);
	  }
  };

  // Get unigram symbol for character.
  Symbol *GetUnigramSymbol(int code, bool trailing);

  // Get bigram symbol for symbol pair.
  Symbol *GetBigramSymbol(Symbol *left, Symbol *right);

  // Adjust frequency for bigram.
  void AdjustBigram(Symbol *left, Symbol *right, int64 delta) {
    GetBigramSymbol(left, right)->freq += delta;
  }

  // Maximum size of word piece lexicon.
  int max_size_;

  // Words in vocabulary.
  std::vector<Word> words_;

  // Mapping from leading/trailing characters to unigram symbols.
  std::unordered_map<int, Symbol *> leading_unigrams_;
  std::unordered_map<int, Symbol *> trailing_unigrams_;

  // Mapping from symbol pairs to bigram symbols.
  std::unordered_map<Bigram, Symbol *, BigramHash> bigrams_;

  // All allocated symbols.
  std::vector<Symbol *> symbols_;
};

}  // namespace nlp
}  // namespace sling

#endif  // SLING_NLP_DOCUMENT_WORDPIECE_BUILDER_H_

