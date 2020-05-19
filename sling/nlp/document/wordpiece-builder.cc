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

#include "sling/nlp/document/wordpiece-builder.h"

#include "sling/util/unicode.h"

namespace sling {
namespace nlp {

void WordPieceBuilder::Symbol::AppendToString(string *str) const {
  if (bigram()) {
    left->AppendToString(str);
    right->AppendToString(str);
  } else if (code == -1) {
    str->append("<UNKNOWN>");
  } else {
    UTF8::Encode(code, str);
  }
}

WordPieceBuilder::~WordPieceBuilder() {
  for (auto *s : symbols_) delete s;
}

void WordPieceBuilder::Build(Vocabulary::Iterator *vocabulary,
                             const Emitter &emit) {
  // Add symbol for OOV token.
  int size = 1;
  Symbol *oov = new Symbol();
  oov->code = -1;
  symbols_.push_back(oov);
  oov->selected = true;
  emit(oov);

  // Create unigram symbols for all words in vocabulary and add initial encoding
  // of all words using character unigrams.
  words_.resize(vocabulary->Size());
  vocabulary->Reset();
  Text str;
  int freq = 0;
  int index = 0;
  while (vocabulary->Next(&str, &freq)) {
    // Encode word as unigram symbols.
    Word &word = words_[index++];
    word.freq = freq;
    const char *p = str.data();
    const char *end = p + str.size();
    bool trailing = false;
    while (p < end) {
      int code = UTF8::Decode(p);

      Symbol *sym = GetUnigramSymbol(code, trailing);
      word.symbols.push_back(sym);
      sym->freq += freq;
      if (!sym->selected) {
        sym->selected = true;
        size++;
        emit(sym);
      }

      p = UTF8::Next(p);
      trailing = true;
    }
  }

  // Create symbols for all character bigrams.
  for (const Word &word : words_) {
    for (int i = 0; i < word.symbols.size() - 1; ++i) {
      Symbol *sym = GetBigramSymbol(word.symbols[i], word.symbols[i + 1]);
      sym->freq += word.freq;
    }
  }

  // Keep adding the most frequent symbol to the output until we reach the
  // maximum size.
  while (size < max_size_) {
    // Find the best symbol, i.e. the symbol with the highest frequency. If the
    // frequency is the same, take shorter symbol. If the length is the same,
    // use lexicographical comparison. If the symbols are the same, take the
    // leading symbol.
    Symbol *best = nullptr;
    for (Symbol *sym : symbols_) {
      if (sym->selected) continue;

      if (best == nullptr || sym->freq > best->freq) {
        best = sym;
      } else if (sym->freq == best->freq) {
        string sym_text = sym->text();
        string best_text = best->text();
        if (sym_text.size() > best_text.size()) {
          best = sym;
        } else if (sym_text.size() == best_text.size()) {
          if (sym_text < best_text) {
            best = sym;
          } else if (sym_text == best_text && !sym->trailing) {
            best = sym;
          }
        }
      }
    }

    // Stop if there are no more symbols to select.
    if (best == nullptr) break;

    // Add best symbol to output lexicon.
    CHECK(best->bigram());
    best->selected = true;
    size++;
    emit(best);

    // Find and replace all instances of best bigram in vocabulary.
    Symbol *left = best->left;
    Symbol *right = best->right;
    for (Word &word : words_) {
      auto &symbols = word.symbols;
      bool again = true;
      while (again) {
        again = false;
        for (int i = 0; i < symbols.size() - 1; ++i) {
          if (symbols[i] == left && symbols[i + 1] == right) {
            // Adjust symbol frequency for overlapping bigrams.
            if (i > 0) {
              // Decrement frequency for (before,left) symbol and increment
              // frequency for (before,best) symbol.
              Symbol *before = symbols[i - 1];
              AdjustBigram(before, left, -word.freq);
              AdjustBigram(before, best, word.freq);
            }

            if (i + 2 < symbols.size()) {
              // Decrement frequency for (right,after) symbol and increment
              // frequency for (best,after) symbol.
              Symbol *after = symbols[i + 2];
              AdjustBigram(right, after, -word.freq);
              AdjustBigram(best, after, word.freq);
            }

            // Replace (left,right) with best.
            symbols[i] = best;
            symbols.erase(symbols.begin() + (i + 1));

            // Repeat to replace other instance of (left,right) in word.
            again = true;
            break;
          }
        }
      }
    }
  }
}

WordPieceBuilder::Symbol *WordPieceBuilder::GetUnigramSymbol(
    int code, bool trailing) {
  // Look up unigram symbol and return this if it already exists.
  if (trailing) {
    auto f = trailing_unigrams_.find(code);
    if (f != trailing_unigrams_.end()) return f->second;
  } else {
    auto f = leading_unigrams_.find(code);
    if (f != leading_unigrams_.end()) return f->second;
  }

  // Create new unigram symbol.
  Symbol *sym = new Symbol();
  sym->code = code;
  sym->trailing = trailing;
  symbols_.push_back(sym);
  if (trailing) {
    trailing_unigrams_[code] = sym;
  } else {
    leading_unigrams_[code] = sym;
  }

  return sym;
}

WordPieceBuilder::Symbol *WordPieceBuilder::GetBigramSymbol(
    Symbol *left, Symbol *right) {
  // Look up bigram symbol and return this if it already exists.
  Bigram bigram(left, right);
  auto f = bigrams_.find(bigram);
  if (f != bigrams_.end()) return f->second;

  // Create new bigram symbol.
  Symbol *sym = new Symbol();
  sym->left = left;
  sym->right = right;
  sym->trailing = left->trailing;
  symbols_.push_back(sym);
  bigrams_[bigram] = sym;

  return sym;
}

}  // namespace nlp
}  // namespace sling
