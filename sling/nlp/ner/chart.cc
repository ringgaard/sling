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

#include "sling/nlp/ner/chart.h"

namespace sling {
namespace nlp {

SpanChart::SpanChart(Document *document, int begin, int end, int maxlen)
    : document_(document), begin_(begin), end_(end), maxlen_(maxlen),
      tracking_(document->store()) {
  // The chart height is equal to the number of tokens.
  size_ = end_ - begin_;

  // Phrase matches cannot be longer than the number of document tokens.
  if (size_ < maxlen_) maxlen_ = size_;

  // Initialize chart.
  items_.resize(size_ * size_);
}

void SpanChart::Add(int begin, int end, Handle match) {
  item(begin - begin_, end - begin).aux = match;
  tracking_.push_back(match);
}

void SpanChart::Populate(const PhraseTable &phrase_table) {
  // Spans cannot start or end on ignored tokens (i.e. punctuation).
  std::vector<bool> skip(size_);
  for (int i = 0; i < size_; ++i) {
    skip[i] = document_->token(i + begin_).Fingerprint() == 1;
  }

  // Find all matching spans up to the maximum length.
  for (int b = begin_; b < end_; ++b) {
    // Span cannot start on a skipped token.
    if (skip[b - begin_]) continue;

    for (int e = b + 1; e <= std::min(b + maxlen_, end_); ++e) {
      // Span cannot end on a skipped token.
      if (skip[e - 1]) continue;

      // Find matches in phrase table.
      uint64 fp = document_->PhraseFingerprint(b, e);
      Item &span = item(b - begin_, e - b);
      span.matches = phrase_table.Find(fp);
      if (span.matches != nullptr) span.cost = 1.0;
    }
  }
}

void SpanChart::Solve() {
  for (int l = 2; l <= size_; ++l) {
    // Find best covering for all spans of length l.
    for (int s = 0; s <= size_ - l; ++s) {
      // Find best split of span [s;s+l).
      Item &span = item(s, l);
      for (int n = 1; n < l; ++n) {
        // Consider the split [s;s+n) and [s+n;s+l).
        Item &left = item(s, n);
        Item &right = item(s + n, l - n);
        float cost = left.cost + right.cost;
        if (cost <= span.cost) {
          span.cost = cost;
          span.split = n;
        }
      }
    }
  }
}

}  // namespace nlp
}  // namespace sling

